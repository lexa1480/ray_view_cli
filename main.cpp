#include <ncurses.h>
#include <vector>
#include <thread>
#include <unistd.h>
#include <mutex>
#include <sys/ioctl.h>
#include <stdio.h>

#include <NitaDataTypes.h>

#include "CommandLineArgs.h"
#include "CommandLineHelpers.h"

#include <ip_st_x3.h>
#include <NITA_Net2/BnpRay.h>

#include <iostream>
#include <string>
#include <time.h>
#include <sys/timeb.h>
#include <fstream>

#include <boost/date_time/microsec_time_clock.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <ModeInfo.h>
plug_key::CModeInfoPlug g_ModeInfoPlug;

std::string GetTimeString_ms()
{
    std::string sTimeIso = boost::posix_time::to_iso_string(boost::posix_time::microsec_clock::universal_time());
    return sTimeIso;
}


//
using namespace std;
using namespace ip_st_x;
using namespace nita_net2;

#include <NDiag.h>
using namespace NDiag;

std::string g_sAddr;
NDiag::CNDiagMain NDiag::g_NDiagMain;

using namespace ray_recv;

#ifndef WIN32
#include <sys/time.h>
unsigned long GetTickCount()
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (tv.tv_sec*1000+tv.tv_usec/1000);
}
#endif//WIN32


class CRaySubscriber : public CStClientSubscriber
{
private:
    bool            m_bMultiLine;
    bool            m_bIgnoreDub;
    bool            m_bLevelsMap;
    bool            m_bShowTime;
    bool            m_bShowStat;
    bool            m_bShowRay;
    bool			m_bShowData;
    bool			m_bShowIdx;
    size_t			m_iRadiusZero;
    size_t			m_iRadiusDistance;
    int				m_iNum;
    int             m_iFirstRayIdx;
    unsigned int	m_uMask;
    NByte			m_byteLevel;
    BNP_UINT16      m_u16NumberPrev;
    bool    m_bConsole;
    std::vector<std::vector<NByte>>* m_vDataVectors;
    std::mutex* m_mutex_lock;

    std::map<unsigned, unsigned> m_mapLevels;

public:
    CRaySubscriber( const boost::program_options::variables_map& vm, std::vector<std::vector<NByte>>& vDataVectors, std::mutex& mutex_lock)
        :     m_bMultiLine(false)
    , m_bIgnoreDub(false)
    , m_bLevelsMap(false)
    , m_bShowTime(true)
    , m_bShowStat(true)
    , m_bShowRay(false)
    , m_bShowData(false)
    , m_bShowIdx(false)
    , m_iRadiusZero(0)
    , m_iRadiusDistance(0)
    , m_iNum(0)
    , m_iFirstRayIdx(0)
    , m_uMask(0)
    , m_byteLevel(0)
    , m_u16NumberPrev(0)
    , m_bConsole(false)
    , m_vDataVectors(&vDataVectors)
    , m_mutex_lock(&mutex_lock)

    {
        m_bMultiLine = false;
        m_bIgnoreDub = IsArgValue( vm, c_szArgNIgnoreDub );
        m_bLevelsMap = IsArgValue( vm, c_szArgNLevelsMap );
        int iLevel = GetArgValue<int>( vm, c_szArgNLevel );
        m_byteLevel = static_cast<NByte>(iLevel);
        m_iRadiusZero =         GetArgValueDefault<size_t>( vm, c_szArgNRadius0, 0 );
        m_iRadiusDistance =         GetArgValueDefault<size_t>( vm, c_szArgNRadius, 0 );
        m_iNum =        GetArgValueDefault<int>( vm, c_szArgNNum, c_iRaysNum );
        m_iFirstRayIdx =	GetArgValueDefault<int>( vm, c_szArgNFirst, c_iRayIdxNegative );
        m_uMask =       GetArgValueDefault<unsigned int>( vm, c_szArgNMask, 0 ); // zero = no mask

        std::vector<std::string> vDisplay;
        GetArgValue< std::vector<std::string> >( vm, c_szArgNDisplay, vDisplay );
        if( !vDisplay.empty() )
        {
            m_bShowTime =   FindArg( vDisplay, c_szArgVTime );
            m_bShowStat =   FindArg( vDisplay, c_szArgVStat );
            m_bShowRay =    FindArg( vDisplay, c_szArgVRay );
            m_bShowData =   FindArg( vDisplay, c_szArgVData );
            m_bShowIdx =	FindArg( vDisplay, c_szArgVIdx );
        }

        if( m_bShowData )
        {
            m_bMultiLine = true;
            m_bShowRay = true;
        }
        m_bConsole = IsArgValue(vm, c_szArgTableCli);
    }


    bool IsMultiLineShow() const {return m_bMultiLine;}
    bool IsLevelsMap() const {return m_bLevelsMap;}

    std::string GetTimeString( time_t ltime = 0, const char* pFormat = "%Y-%m-%d_%H:%M:%S" )
    {
        if( 0 == ltime )
            time( &ltime );
        struct tm *ptmToday = gmtime( &ltime );
        char szTime[128] = "";
        strftime( szTime, sizeof( szTime ), pFormat, ptmToday );
        return std::string( szTime );
    }
    void PrintLevelsMap(std::ostream& ss )
    {
        ss << "LEVEL<>COUNT---------------------" << std::endl;
        std::map<unsigned, unsigned>::iterator it;
        for(it = m_mapLevels.begin(); it != m_mapLevels.end(); ++it)
        {
            ss << std::setw(3) << it->first << ", " << std::setw(6) << it->second << std::endl;
        }
    }
    void PrintTime(std::stringstream& ss)
    {
        if( m_bShowTime )
            ss  << GetTimeString_ms() << ">";
    }
    void PrintHeader(std::stringstream& ss, CDataPacketBuffer& dpb )
    {
        ss  << "["  << dpb.GetBaseHeader()->GetComputerName()
            << "#"  << std::setfill('0') << std::setw(5) << dpb.GetBaseHeader()->m_wPacketNum
            << "]";
    }
    void PrintRay( std::stringstream& ss, const nita_net2::CbnpRay& ray, size_t& szSize, BNP_FLOAT32& fMeterScale )
    {
        BNP_UINT16 uNum = 0;
        if( ray.uDataMask & RDM_Number )
            uNum = ray.uNumber;
        ss << " #" << std::setfill('0') << std::setw(4) << uNum;
        bool bReverseRay = IsRayIgnoredByReverse(m_u16NumberPrev, ray.uNumber, 4096, 100);
        if( m_u16NumberPrev == ray.uNumber )
            ss << "*";
        if( bReverseRay )
            ss << "-";
        else
            ss << " ";
        szSize = 0;
        if( ray.uDataMask & RDM_RayData )
            szSize = ray.vectorData.size();
        ss << " size:" << std::setfill('0') << std::setw(5) << szSize << ";";
        fMeterScale = 0;
        if( ray.uDataMask & RDM_MeterScale )
            fMeterScale = ray.fMeterScale;
        ss << " scale:" << std::fixed << std::setprecision(1) << fMeterScale << ";";
        BNP_UINT16 u16BitDepth = 0;
        if( ray.uDataMask & RDM_Property )
        {
            BNP_UINT16 u16BitDepthProperty = ( ray.uProperty & Property_Resolution_Enum );
            if( u16BitDepthProperty )
            {
                if(u16BitDepthProperty & Property_Resolution_4bit)
                       u16BitDepth = 4;
                if(u16BitDepthProperty & Property_Resolution_8bit)
                       u16BitDepth = 8;
            }
        }
        ss << " depth:" << u16BitDepth << ";";
    }

        void OnRayIdx( int iRayNum )
    {
        if( m_iFirstRayIdx == iRayNum )
        {
            m_iFirstRayIdx = c_iRayIdxNegative;
        }

        if( m_iNum > 0 )
        {
            if( m_iFirstRayIdx == c_iRayIdxNegative )
                m_iNum--;
        }
    }
    void OnMissedRayCount( std::stringstream& ss, int iRayNum, unsigned uCount )
    {
        if( IsMultiLineShow() )
        {
            if( m_bShowRay && !m_bConsole)
                ss << " #" << std::setfill('0') << std::setw(4) << iRayNum << " - missed: " << uCount << std::endl;
        }
    }
    void OnMissedRay( std::stringstream& ss, int iRayNum )
    {
        if( IsMultiLineShow() )
        {
            if( m_bShowRay && !m_bConsole)
                ss << " #" << std::setfill('0') << std::setw(4) << iRayNum << " - missed;" << std::endl;
        }
    }
    void MapLevels( const nita_net2::CbnpRay& ray )
    {
        for(size_t i=0; i<ray.vectorData.size(); i++)
        {
            unsigned uDot = ray.vectorData[i];
            if( m_mapLevels.find(uDot) != m_mapLevels.end() )
                m_mapLevels[uDot] += 1;
            else
                m_mapLevels[uDot] = 1;
        }
    }
    bool PrintLevels( std::stringstream& ss, const nita_net2::CbnpRay& ray )
    {
        bool bRes = false;
        if( IsMultiLineShow() )
        {
            for(size_t i=0; i<ray.vectorData.size(); i++)
            {
                if( i < m_iRadiusZero )
                    continue;
                if( ( m_iRadiusDistance > 0 ) && ( ( i - m_iRadiusZero ) > m_iRadiusDistance ) )
                    break;
                NByte byteDot = ray.vectorData[i];
                if( m_uMask )
                    byteDot = ( byteDot & m_uMask );
                bRes = true;
                ss << ' ';
                if( m_bShowIdx )
                    ss << std::dec << i << ':';
                if( m_bShowData )
                {
                    if( byteDot >= m_byteLevel )
                        ss << std::hex << std::setfill('0') << std::setw(2) << (unsigned)byteDot;
                    else
                        ss << "--";
                }
            }
        }
        return bRes;
    }

    void Print( std::stringstream& ss, unsigned long& lTimePrev_ticks, bool bPrintForced )
    {
        unsigned long lTimeCur_ticks = GetTickCount();
        unsigned long lTimeout_ms = lTimeCur_ticks - lTimePrev_ticks;
        if( bPrintForced || (lTimeout_ms > 200) )
        {
            lTimePrev_ticks = lTimeCur_ticks;
            if( m_bShowRay && !IsMultiLineShow() )
                ss << '\r';
            std::cout << ss.str() << std::flush;
        }
    }

    unsigned GetLostCount( CDataPacketBuffer& dpb, NWord& wPacketNum )
    {
        unsigned uLost = 0;
        CPacketBaseHeader* pHeader = dpb.GetBaseHeader();
        if( wPacketNum )
        {
            if( pHeader->m_wPacketNum > wPacketNum )
            {
                uLost = pHeader->m_wPacketNum - wPacketNum;
                if(uLost > 0)
                    uLost -= 1;     // normal count
                if( uLost > 2000 )  // sender restart?
                    uLost = 0;
            }
        }
        wPacketNum = pHeader->m_wPacketNum;
        return uLost;
    }

    bool IsRayIgnoredByReverse( int iPrev_num, int iNext_num, int iTurn_num, int iIgnore_num )
    {
        bool bIgnore = false;
        if( iNext_num == iPrev_num )
            bIgnore = true;
        else
        {
            int iBackIdx = iPrev_num - iIgnore_num;
            if( iBackIdx < 0 )
                iBackIdx += iTurn_num;
            if( iBackIdx < iPrev_num )
                bIgnore = ( iBackIdx <= iNext_num && iNext_num <= iPrev_num );
            else
                bIgnore = ( iBackIdx <= iNext_num || iNext_num <= iPrev_num );
        }
        return bIgnore;
    }

    virtual NVoid OnPacket( NByte* lpData, NDword dwDataSize, CDataPacketBuffer& dpb )
    {
        static unsigned long lTimeObserv_ticks = GetTickCount();
        static unsigned uRays = 0;
        static unsigned uPackets = 0;
        static unsigned uErrorsCount = 0;
        static unsigned long lTimeRay_ticks = GetTickCount();
        static NWord	wPacketNum = 0;
        static unsigned uLostTotal = 0;
        static size_t szSize = 0;
        static BNP_FLOAT32 fMeterScale = 0;
        static BNP_UINT16	u16RayNumDiff = 0;

        bool bIgrnoreRay = false;

        uPackets++;
        unsigned uLost = GetLostCount( dpb, wPacketNum );
        uLostTotal += uLost;

        std::stringstream ss;

        bool bPrintForced = false;
        CBaseNetProtocol bnp(lpData, dwDataSize);
        if( bnp.IsValid()
                && bnp.m_uCategory == BNP_Cat_RadarRay )
        {
            uRays++;

            nita_net2::CbnpRay ray;
            ray.InitByMemStream(bnp.m_streamIn);

            if( m_u16NumberPrev > ray.uNumber )
            {
                CPacketBaseHeader* pHeader = dpb.GetBaseHeader();
                long lTimeCur_ticks = GetTickCount();
                long lDeltaTime_ms = lTimeCur_ticks - lTimeObserv_ticks;
                lTimeObserv_ticks = lTimeCur_ticks;
                if( m_bShowStat )
                {
                    PrintTime(ss);
                    ss      << "Turn> "     << pHeader->m_szComputerName
                            << "; "         << std::setfill(' ') << std::setw(4) << lDeltaTime_ms << " ms"
                            << "; rays:"    << std::setfill(' ') << std::setw(4) << uRays
                            << "; num_diff:" << u16RayNumDiff
                            << "; size:"    << std::setfill('0') << std::setw(4) << szSize
                            << "; scale:"   << fMeterScale
                            << "; packet:"  << std::setw(5) << std::setfill('0') << (int)wPacketNum
                            << "; lost:"    << uLostTotal
                            << "; errors:"  << uErrorsCount
                            << "; reverse: " << m_u16NumberPrev << " - " << ray.uNumber << ""
                            << std::endl;
                }
                uRays = 0;
                uLostTotal = 0;
                u16RayNumDiff = 0;
                bPrintForced = true;
            }

            int iPrev = ( m_u16NumberPrev > ray.uNumber ) ? (m_u16NumberPrev-4096) : m_u16NumberPrev;
            BNP_UINT16 u16NumDiff = BNP_UINT16(ray.uNumber - iPrev);

            if( m_bIgnoreDub && ( 0 == u16NumDiff) )
                bIgrnoreRay = true;

            if( u16RayNumDiff < u16NumDiff )
                u16RayNumDiff = u16NumDiff;
            if( u16NumDiff <= 5 )
            {
                for( int k = iPrev+1; k<ray.uNumber; k++ )
                {
                    int iRayNum = (k>=0) ? k : (k+4096);
                    OnMissedRay(ss, iRayNum);
                    OnRayIdx(iRayNum);
                }
            }
            else if(u16NumDiff > 5)
                OnMissedRayCount(ss, m_u16NumberPrev+1, u16NumDiff);

            if( !bIgrnoreRay )
            {
                OnRayIdx(ray.uNumber);
                MapLevels(ray);
                if(m_bConsole)
                {
                    m_mutex_lock->lock();
                    m_vDataVectors->at(ray.uNumber/2) = ray.vectorData;
                    m_mutex_lock->unlock();

                    bPrintForced = true;
                }
                else
                {
                    if( m_bShowRay )
                    {
                        PrintTime(ss);
                        PrintHeader(ss, dpb );
                        PrintRay(ss, ray, szSize, fMeterScale );
                        if( PrintLevels( ss, ray ) )
                        {
                            ss << std::endl;
                            bPrintForced = true;
                        }
                    }
                }
            }
            m_u16NumberPrev = ray.uNumber;
        }
        else
        {
            ss << "Cat#" << (int)bnp.m_uCategory << "> " << uPackets << "; size:" << dwDataSize << "; lost:" << uLostTotal;
            ss << "; ERROR - wrong category!";
            uErrorsCount++;
        }

        bool bPrintEnabled = true;

        if( m_iNum == 0 )
        {
            bPrintEnabled = false;
        }
        else if( m_iFirstRayIdx != c_iRayIdxNegative )
        {
            bPrintEnabled = false;
        }

        if( bPrintEnabled )
            Print( ss, lTimeRay_ticks, bPrintForced );
    }
};

static const int VSize = 2048;
static const int HSize = 1024;

bool PRINT(int X, int Y, int HQSize, int VQSize, std::vector<std::vector<NByte>>& vDataVectors)//X,Y - Left Up square
{
    for(int i = Y; i<(Y+VQSize); i++)
    {
        attron(COLOR_PAIR(1));
        printw("Ray%4i: ", i);
        if(vDataVectors.at(0).size() != 0)
        {
            for(int j = X; j<(X+HQSize); j++)
            {
                switch ( vDataVectors.at(i).at(j) )
                {
                case 0:
                attron(COLOR_PAIR(4));
                break;

                case 3:
                attron(COLOR_PAIR(3));
                break;

                case 10:
                attron(COLOR_PAIR(6));
                break;

                case 15:
                attron(COLOR_PAIR(2));
                break;

                case 31:
                attron(COLOR_PAIR(7));
                break;

                case 47:
                attron(COLOR_PAIR(8));
                break;
                }

                printw("%2.2X", vDataVectors.at(i).at(j));
            }
        }
        printw("\n");
    }

    attron(COLOR_PAIR(1));
    int  column = X;
    for(int j = 9; j<((2*HQSize)+10); j++)
    {
        move(VQSize, j);
        if( (column%10) == 0 )
        {
            printw("|%4.4i", column);
            j+=4;
            column+=2;
        }
        else
        {
            if( (j%2) == 1)
            {
                printw(";");
            }
            else
            {
                printw(",");
                column++;
            }
        }
    }
    move(0, 0);

return true;
}

void WindowMove(int x, int y, std::vector<std::vector<NByte>>& vDataVectors, std::mutex& mutex_lock)
{
    bool bExit = false;
    int HSqSize = (getmaxx(stdscr) - 10)/2;
    int VSqSize = getmaxy(stdscr) - 1;

    while ( !bExit )
    {
        clear();
        mutex_lock.lock();
        PRINT(x, y, HSqSize, VSqSize, vDataVectors);
        mutex_lock.unlock();
        refresh();

        int ch = getch();

        switch ( ch )
        {
        case 10: //Enter
        bExit = true;
        break;

        case KEY_LEFT: //Влево
        if(x>0)
            x--;
        break;

        case KEY_UP: //Вверх
        if(y>0)
            y--;
        break;

        case KEY_RIGHT: //Вправо
        if(x<(HSize - HSqSize))
            x++;
        break;

        case KEY_DOWN: //Вниз
        if(y<(VSize - VSqSize))
            y++;
        break;

        case 547: //Влево + Ctrl
        if(x>=HSqSize)
            x = x - HSqSize;
        break;

        case 568: //Вверх + Ctrl
        if(y>=VSqSize)
            y = y - VSqSize;
        break;

        case 562: //Вправо + Ctrl
        if(x<=(HSize - (2*HSqSize)))
            x = x + HSqSize;
        break;

        case 527: //Вниз + Ctrl
        if(y<=(VSize - (2*VSqSize)))
            y = y + VSqSize;
        break;
        }
    }
}

int main(int argc, char *argv[])
{
    boost::program_options::variables_map vm;
    if( !CheckCommandLineArgs( argc, argv, vm ) )
        return 1;

    std::string sSource = GetArgValue<std::string>( vm, c_szArgNSource);

    std::string sApp = "RayRecv";
    if( !g_sAddr.empty() )
        sApp += "-" + g_sAddr;
    CNDiagMain* pNDiag = CNDiagMain::GetObj();
    std::cout << "INF> Open NDiag" << std::endl;
    pNDiag->Open( sApp.c_str(), sApp.c_str(), "1.0.0" );

    g_ModeInfoPlug.Load();

    CStPlugMain		stPlugMain;
    CStPlugClient	stClient;

    vector<vector<NByte>> vDataVectors(VSize, vector<NByte> (0, 0));//
    std::mutex mutex_lock;
    CRaySubscriber  stRaySubscriber(vm, vDataVectors, mutex_lock);

    //Plugin Load test
    std::cout << "INF> Load ip_st" << std::endl;
    if( stPlugMain.Load() )
    {
        stPlugMain.SetAppName( sApp.c_str() );

        bool bModePlay = IsArgValue( vm, c_szArgNModePlay );
        if( bModePlay )
            stPlugMain.SetPlayMode(true);

        std::cout << "INF> Start ip_st" << std::endl;
        if( !stPlugMain.Start() )
        {
            std::cout << "ERR> Can't start ip_st plugin" << std::endl;
        }
        if( !g_sAddr.empty() )
            stPlugMain.SetSockMcAddr( g_sAddr.c_str() );

        //Create client
        if( !stPlugMain.GetStClient( stClient ) )
        {
            std::cout << "ERR> Can't get ip_st client" << std::endl;
        }

        stClient.SetClientName( "tstClient" );

        //Assign channel
        if( !stClient.AssignSource( sSource.c_str(), ST_READER ) )
        {
            std::cout << "ERR> Can't assign ip_st source " << sSource << std::endl;
        }

        stClient.EnableOwnPacketsProcess( true );
        stClient.Subscribe( &stRaySubscriber );

        //Open client
        if( !stClient.OpenStClient() )
        {
            std::cout << "ERR> Can't open ip_st client for " << sSource << std::endl;
        }

        std::cout << "INF> Press <Enter> to exit" << std::endl;

        if(IsArgValue(vm, c_szArgTableCli))
        {
            int iCoordX = 0;
            int iCoordY = 0;
            initscr();
            keypad(stdscr, true);
            noecho();
            nodelay(stdscr, true);
            halfdelay(10);
            wtimeout(stdscr, -1000);

            start_color();
            init_pair(1, COLOR_WHITE, COLOR_BLACK);
            init_pair(2, COLOR_RED, COLOR_BLACK);
            init_pair(3, COLOR_GREEN, COLOR_BLACK);
            init_pair(4, COLOR_BLUE, COLOR_BLACK);
            init_pair(5, COLOR_CYAN, COLOR_BLACK);
            init_pair(6, COLOR_YELLOW, COLOR_BLACK);
            init_pair(7, COLOR_MAGENTA, COLOR_BLACK);

            std::thread thread(WindowMove, std::ref(iCoordX), std::ref(iCoordY), std::ref(vDataVectors), std::ref(mutex_lock));
            thread.join();
            endwin();
        }
        else
        {
            getchar();
        }


        if( stRaySubscriber.IsLevelsMap() )
        {
            std::string sFilePath = "./levels.csv";
            std::ofstream file( sFilePath.c_str() );
            if( !file )
            {
                std::cerr << "ERR> Can't open file " << sFilePath << std::endl;
            }
            stRaySubscriber.PrintLevelsMap( file );
        }
        //close client
        stClient.UnSubscribe();
        stClient.CloseStClient();

        //Free plugin
        std::cout << "INF> Stop ip_st" << std::endl;
        stPlugMain.Stop();
        std::cout << "INF> Free ip_st" << std::endl;
        stPlugMain.Free();
    }
    else
    {
        std::cout << "ERR> Can't load ip_st plugin" << std::endl;
    }
    g_ModeInfoPlug.Free();

    std::cout << "INF> Close NDiag" << std::endl;
    pNDiag->Close();

}
