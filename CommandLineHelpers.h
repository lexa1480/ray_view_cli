#pragma once

#include <boost/program_options.hpp>

#include <string>
#include <vector>

inline bool IsArgValue( const boost::program_options::variables_map& vm, const char* pArg )
{
    return ( vm.count(pArg) > 0 );
}

template<typename T>
inline bool GetArgValue( const boost::program_options::variables_map& vm, const char* pArg, T& t )
{
    bool bRes = false;
    if( vm.count(pArg) > 0 )
    {
        t = vm[pArg].as<T>();
        bRes = true;
    }
    return bRes;
}

template<typename T>
inline T GetArgValue( const boost::program_options::variables_map& vm, const char* pArg )
{
    T t;
    GetArgValue<T>( vm, pArg, t );
    return t;
}

template<typename T>
inline T GetArgValueDefault( const boost::program_options::variables_map& vm, const char* pArg, T tDef )
{
    T t = tDef;
    GetArgValue<T>( vm, pArg, t );
    return t;
}

inline bool FindArg( const std::vector<std::string>& vType, const char* pArgType )
{
    bool bRes = ( std::find( vType.begin(),vType.end(), pArgType ) != vType.end() );
    return bRes;
}
