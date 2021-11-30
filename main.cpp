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



int SetColor(int iCell)
{
    int palette_idx = 0;
    if( iCell <= int(255/2) )
    {
        int palette_first = 0x11;
        int palette_last  = 0x34;
        int palette_range = palette_last - palette_first;
        palette_idx = int( palette_first + palette_range * iCell * 2 / 255 );
    }
    else
    {
        if( (iCell > int(255/2)) && (iCell <= 255) )
        {
            int palette_first = 0xe8;
            int palette_last  = 0xc5;
            int palette_range = palette_last - palette_first;
            palette_idx = int( palette_first + palette_range * (iCell-255/2) * 2 / 255 );
        }
    }
    return palette_idx;
}

void PrintPad(WINDOW* pad, int row, vector<NByte> vector)
{
    wmove(pad, row, 0);
    for(int i = 0; i<(int)vector.size();i++)
    {
        wattron(pad, COLOR_PAIR(SetColor(vector.at(i))));
        wprintw(pad, "%2.2X", vector.at(i));
    }

    wprintw(pad, "\n");
}



class CRaySubscriber : public CStClientSubscriber
{
private:
    size_t			m_iRadiusZero;
    size_t			m_iRadiusDistance;
    int				m_iNum;
    int             m_iFirstRayIdx;
    unsigned int	m_uMask;
    NByte			m_byteLevel;
    BNP_UINT16      m_u16NumberPrev;
    WINDOW* m_pad;
    std::mutex* m_mutex_lock;

public:
    CRaySubscriber( const boost::program_options::variables_map& vm, WINDOW* pad, std::mutex& mutex_lock)
        :     m_iRadiusZero(0)
    , m_iRadiusDistance(0)
    , m_iNum(0)
    , m_iFirstRayIdx(0)
    , m_uMask(0)
    , m_byteLevel(0)
    , m_u16NumberPrev(0)
    , m_pad(pad)
    , m_mutex_lock(&mutex_lock)

    {
        int iLevel = GetArgValue<int>( vm, c_szArgNLevel );
        m_byteLevel = static_cast<NByte>(iLevel);
        m_iRadiusZero =         GetArgValueDefault<size_t>( vm, c_szArgNRadius0, 0 );z
        m_iRadiusDistance =         GetArgValueDefault<size_t>( vm, c_szArgNRadius, 0 );
        m_iNum =        GetArgValueDefault<int>( vm, c_szArgNNum, c_iRaysNum );
        m_iFirstRayIdx =	GetArgValueDefault<int>( vm, c_szArgNFirst, c_iRayIdxNegative );
        m_uMask =       GetArgValueDefault<unsigned int>( vm, c_szArgNMask, 0 ); // zero = no mask
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
               // ss << " #" << std::setfill('0') << std::setw(4) << iRayNum << " - missed: " << uCount << std::endl;
    }
    void OnMissedRay( std::stringstream& ss, int iRayNum )
    {
                //ss << " #" << std::setfill('0') << std::setw(4) << iRayNum << " - missed;" << std::endl;
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

    virtual NVoid OnPacket( NByte* lpData, NDword dwDataSize, CDataPacketBuffer& dpb )
    {
        static unsigned uRays = 0;
        static unsigned uPackets = 0;
        static unsigned uErrorsCount = 0;
        static NWord	wPacketNum = 0;
        static unsigned uLostTotal = 0;
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
                uRays = 0;
                uLostTotal = 0;
                u16RayNumDiff = 0;
                bPrintForced = true;
            }

            int iPrev = ( m_u16NumberPrev > ray.uNumber ) ? (m_u16NumberPrev-4096) : m_u16NumberPrev;
            BNP_UINT16 u16NumDiff = BNP_UINT16(ray.uNumber - iPrev);

            if(  0 == u16NumDiff )
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

                m_mutex_lock->lock();
                PrintPad(m_pad, ray.uNumber/2, ray.vectorData);
                m_mutex_lock->unlock();

                bPrintForced = true;
            }
            m_u16NumberPrev = ray.uNumber;
        }
        else
        {
            ss << "Cat#" << (int)bnp.m_uCategory << "> " << uPackets << "; size:" << dwDataSize << "; lost:" << uLostTotal;
            ss << "; ERROR - wrong category!";
            std::cout << ss.str() << std::flush;
            uErrorsCount++;
        }
    }
};

static const int VSize = 2048;
static const int HSize = 1024;

bool SetPadHorizontal(WINDOW* padHorizontal)
{
    wprintw(padHorizontal, "         ");
    for(int j = 0; j<HSize; j+=10)
    {
        wprintw(padHorizontal, "|%4.4i,;,;,;,;,;,;,;,", j);
    }


    return true;
}

bool SetPadVertical(WINDOW* padVertical)
{
    werase(padVertical);
    for(int i = 0; i<VSize; i++)
    {
        wmove(padVertical, i, 0);
        wprintw(padVertical, "Ray%4i:", i);
    }

    return true;
}

void WindowMove(WINDOW* pad, std::mutex& mutex_lock, WINDOW* padHorizontal, WINDOW* padVertical)
{
    bool bExit = false;
    int x = 0;
    int y = 0;
    int HSqSize = getmaxx(stdscr);
    int VSqSize = getmaxy(stdscr);
    mutex_lock.lock();
    SetPadHorizontal(padHorizontal);
    SetPadVertical(padVertical);
    mutex_lock.unlock();

    while ( !bExit )
    {
        mutex_lock.lock();
        prefresh(pad, y, x, 0, 9, VSqSize - 2, HSqSize - 1);
        mutex_lock.unlock();
        prefresh(padHorizontal, 0, x, VSqSize - 1, 0, VSqSize, HSqSize - 1);
        prefresh(padVertical, y, 0, 0, 0, VSqSize - 2, 9);

        int ch = getch();

        switch ( ch )
        {
        case 10: //Enter
        bExit = true;
        break;

        case KEY_LEFT: //Влево
        if(x>0)
            x-=2;
        break;

        case KEY_UP: //Вверх
        if(y>0)
            y--;
        break;

        case KEY_RIGHT: //Вправо
        if(x<((2*HSize+9) - HSqSize))
            x+=2;
        break;

        case KEY_DOWN: //Вниз
        if(y<((VSize+1) - VSqSize))
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
        if(x<=((2*HSize+10) - (2*HSqSize)))
            x = x + HSqSize;
        break;

        case 527: //Вниз + Ctrl
        if(y<=((VSize+1) - (2*VSqSize)))
            y = y + VSqSize;
        break;



        case KEY_HOME: //Начало
        x = 0;
        y = 0;
        break;

        case KEY_END: //Конец
        x = (2*HSize+9) - HSqSize;
        y = (VSize+1) - VSqSize;
        break;



        case KEY_RESIZE: //Изменение размера окна
        int Hor = getmaxx(stdscr);
        int Ver = getmaxy(stdscr);
        if(x > ((2*HSize+9) - Hor))
        {
            x = ((2*HSize+9) - Hor);
        }
        if(y>((VSize+1) - Ver))
        {
            y = ((VSize+1) - Ver);
        }

        HSqSize = Hor;
        VSqSize = Ver;
        redrawwin(stdscr);
        refresh();
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

    initscr();
    keypad(stdscr, true);
    noecho();
    halfdelay(10);
    start_color();
    for(int i = 0; i < COLORS; i++)
    {
        init_pair(i + 1, i, 0);
    }
    init_pair(1, COLOR_WHITE, COLOR_BLUE);
    bkgdset( COLOR_PAIR(1) );
    WINDOW* pad = newpad(VSize + 1, 2*HSize + 10);
    std::mutex mutex_lock;

    CRaySubscriber  stRaySubscriber(vm, pad, mutex_lock);

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

        clear();
        WINDOW* padHorizontal = newpad(1, 2*HSize + 10);
        WINDOW* padVertical = newpad(VSize, 9);

        std::thread thread(WindowMove, std::ref(pad), std::ref(mutex_lock), std::ref(padHorizontal), std::ref(padVertical));
        thread.join();
        endwin();

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
