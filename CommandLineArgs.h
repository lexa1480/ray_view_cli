#pragma once

#include <boost/program_options.hpp>

#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace ray_recv
{
// Names
const char c_szArgNSource[] =   "source";
const char c_szArgNDisplay[] =	"display";
const char c_szArgNLevel[] =    "level";
const char c_szArgNMask[] =     "mask";
const char c_szArgNRadius0[] =  "r0";
const char c_szArgNRadius[] =   "radius";
const char c_szArgNNum[] =		"number";
const char c_szArgNFirst[] =	"first_ray";
const char c_szArgNIgnoreDub[] ="ignore_dub";
const char c_szArgNLevelsMap[] ="levels_map";
const char c_szArgNModePlay[] = "mode_play";
const char c_szArgConsoleData[] = "console";

// Values
const char c_szArgVTime[] =		"time"; // show time marks
const char c_szArgVStat[] =		"stat"; // show statistics on every turn
const char c_szArgVRay[] =		"ray";  // show ray info
const char c_szArgVIdx[] =		"idx";
const char c_szArgVData[] =		"data";

const int   c_iRaysNum =        -1;
const int   c_iRayIdxNegative = -1;

inline bool CheckCommandLineArgs(int ac, char* av[], boost::program_options::variables_map& vm )
{
    bool bContinueExecution = false;

    namespace po = boost::program_options;
    try
    {
        boost::program_options::options_description desc("Command line arguments");
        desc.add_options()
                ("help,h",												"show help")
                ("source,s",	po::value<std::string>()
                                        ->default_value("BnpRay11"),	"set source name; example: --source=BnpRay11")
                ("display,d",	po::value< std::vector<std::string> >()
                                    ->multitoken(),						"set items to display; example: --display time stat ray idx data")
                ("level,l",		po::value<int>()
                                        ->default_value(0),				"set minimum level")
                ("mask,m",		po::value<unsigned>()
                                        ->default_value(0),				"set mask to show filtered points (zero = no mask)")
                ("number,n",	po::value<int>()
                                        ->default_value(c_iRaysNum),	"set number of rays to show (-1 = show all rays)")
                ("first_ray,f",	po::value<int>()
                                        ->default_value(c_iRayIdxNegative),   "first ray index to count number of rays (--number)")
                ("r0,0",		po::value<size_t>()
                                        ->default_value(0),				"set minimum distance point index to show")
                ("radius,r",	po::value<size_t>()
                                        ->default_value(0),				"set number of points - radius distance - to show")
                (c_szArgNIgnoreDub,                                     "ignore rays with dublicated index")
                (c_szArgNLevelsMap,                                     "count values of levels")
                (c_szArgNModePlay,                                      "set ip_st in play mode")
                (c_szArgConsoleData,                                      "get data in console")
                ;

        po::parsed_options parsed = po::command_line_parser(ac, av).
            options(desc).allow_unregistered().run();
        po::store(parsed, vm);

        if( vm.count("help") || ( vm.size() == 0 ) )
            std::cout << desc << "\n";
        else
        {
            po::notify(vm);
            bContinueExecution = true;
        }
    }
    catch(std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
    }
    catch(...)
    {
        std::cerr << "Exception of unknown type!\n";
    }

    return bContinueExecution;
}

}//namespace ray_recv
