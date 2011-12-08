#include "ScriptEngine.h"
#include <PlatformSpecific.h>
#include <Windows.h>
#include <stdio.h>
#include <tchar.h>
#include <ScriptPragmas.h>
#include <AngelScript/angelscript.h>
#include <AngelScript/Preprocessor/preprocess.h>
#include <list>
#include <set>
#include <strstream>
#include <algorithm>
#include <locale.h>
using namespace std;

#ifdef FO_LINUX
# define _stricmp    strcasecmp
#endif

typedef int ( __cdecl * BindFunc )( asIScriptEngine* );
typedef asIScriptEngine* ( __cdecl * ASCreate )();
typedef bool ( *PragmaCallbackFunc )( const char* );

asIScriptEngine*                    Engine = NULL;
bool                                IsServer = false;
bool                                IsClient = false;
bool                                IsMapper = false;
char*                               Buf = NULL;
Preprocessor::LineNumberTranslator* LNT = NULL;

const char*                         ContextStatesStr[] =
{
    "Finished",
    "Suspended",
    "Aborted",
    "Exception",
    "Prepared",
    "Uninitialized",
    "Active",
    "Error",
};

void Global_Log( string& str )
{
    printf( "%s\n", str.c_str() );
}

void PrintContextCallstack( asIScriptContext* ctx )
{
    int                      line, column;
    const asIScriptFunction* func;
    int                      stack_size = ctx->GetCallstackSize();
    printf( "State<%s>, call stack<%d>:\n", ContextStatesStr[ (int) ctx->GetState() ], stack_size );

    // Print current function
    if( ctx->GetState() == asEXECUTION_EXCEPTION )
    {
        line = ctx->GetExceptionLineNumber( &column );
        func = Engine->GetFunctionById( ctx->GetExceptionFunction() );
    }
    else
    {
        line = ctx->GetLineNumber( 0, &column );
        func = ctx->GetFunction( 0 );
    }
    if( func )
        printf( "  %d) %s : %s : %d, %d.\n", stack_size - 1, func->GetModuleName(), func->GetDeclaration(), line, column );

    // Print call stack
    for( int i = 1; i < stack_size; i++ )
    {
        func = ctx->GetFunction( i );
        line = ctx->GetLineNumber( i, &column );
        if( func )
            printf( "  %d) %s : %s : %d, %d.\n", stack_size - i - 1, func->GetModuleName(), func->GetDeclaration(), line, column );
    }
}

void RunMain( asIScriptModule* module, const char* func_str )
{
    // Make function declaration
    char func_decl[ 1024 ];
    sprintf( func_decl, "void %s()", func_str );

    // Run
    printf( "Executing '%s'.\n", func_str );
    asIScriptContext* ctx = Engine->CreateContext();
    int               func = module->GetFunctionIdByDecl( func_decl );
    if( func < 0 )
    {
        printf( "Function '%s' not found.\n", func_decl );
        return;
    }
    int result = ctx->Prepare( func );
    if( result < 0 )
    {
        printf( "Context preparation failure, error code<%d>.\n", result );
        return;
    }

    result = ctx->Execute();
    asEContextState state = ctx->GetState();
    if( state != asEXECUTION_FINISHED )
    {
        if( state == asEXECUTION_EXCEPTION )
            printf( "Execution of script stopped due to exception.\n" );
        else if( state == asEXECUTION_SUSPENDED )
            printf( "Execution of script stopped due to timeout.\n" );
        else
            printf( "Execution of script stopped due to %s.\n", ContextStatesStr[ (int) state ] );
        PrintContextCallstack( ctx );
        ctx->Abort();
        return;
    }

    if( result < 0 )
    {
        printf( "Execution error<%d>, state<%s>.\n", result, ContextStatesStr[ (int) state ] );
    }
    printf( "Execution finished.\n" );
}

void* GetScriptEngine()
{
    return Engine;
}

void CallBack( const asSMessageInfo* msg, void* param )
{
    const char* type = "ERROR";
    if( msg->type == asMSGTYPE_WARNING )
        type = "WARNING";
    else if( msg->type == asMSGTYPE_INFORMATION )
        type = "INFO";

    if( msg->type != asMSGTYPE_INFORMATION )
    {
        if( msg->row )
        {
            printf( "%s(%d) : %s : %s.\n", LNT->ResolveOriginalFile( msg->row ).c_str(), LNT->ResolveOriginalLine( msg->row ), type, msg->message );
        }
        else
        {
            printf( "%s : %s.\n", type, msg->message );
        }
    }
    else
    {
        printf( "%s(%d) : %s : %s.\n", LNT->ResolveOriginalFile( msg->row ).c_str(), LNT->ResolveOriginalLine( msg->row ), type, msg->message );
    }
}

int _tmain( int argc, _TCHAR* argv[] )
{
    /************************************************************************/
    /* Parameters                                                           */
    /************************************************************************/
    setlocale( LC_ALL, "Russian" );

    if( argc < 4 )
    {
        printf( "Not enough parameters. Example:\n"
                "ASCompiler script_name.fos as.dll fo[_client/_mapper].dll\n"
                " [-p preprocessor_output.txt]\n"
                " [-d SOME_DEFINE]*\n"
                " [-run func_name]*\n"
                "*can be used over and over again" );
        return 0;
    }

    char* str_fname = argv[ 1 ];
    char  str_comp_dll[ 1024 ];
    strcpy_s( str_comp_dll, argv[ 2 ] );
    char  str_script_dll[ 1024 ];
    strcpy_s( str_script_dll, argv[ 3 ] );

    char*           str_prep = NULL;
    vector< char* > defines;
    vector< char* > run_func;
    for( int i = 4; i < argc; i++ )
    {
        // Preprocessor output
        if( !_stricmp( argv[ i ], "-p" ) && i + 1 < argc )
            str_prep = argv[ ++i ];
        // Define
        else if( !_stricmp( argv[ i ], "-d" ) && i + 1 < argc )
            defines.push_back( argv[ ++i ] );
        // Run function
        else if( !_stricmp( argv[ i ], "-run" ) && i + 1 < argc )
            run_func.push_back( argv[ ++i ] );
    }

    #if defined ( FO_X64 )
    strcat_s( str_comp_dll, "64" );
    strcat_s( str_script_dll, "64" );
    #endif

    /************************************************************************/
    /* Dll                                                                  */
    /************************************************************************/
    // Engine
    HINSTANCE comp_dll;
    if( ( comp_dll = LoadLibrary( str_comp_dll ) ) == NULL )
    {
        printf( "AngelScript library<%s> loading fail.\n", str_comp_dll );
        return 0;
    }

    ASCreate create_func;
    if( !( create_func = (ASCreate) GetProcAddress( comp_dll, "Register" ) ) )
    {
        printf( "Function Register not found, error<%u>.\n", GetLastError() );
        return 0;
    }

    Engine = ( *create_func )( );
    if( !Engine )
    {
        printf( "Register failed.\n" );
        return 0;
    }
    Engine->SetMessageCallback( asFUNCTION( CallBack ), NULL, asCALL_CDECL );

    // Bind
    HINSTANCE script_dll;
    if( ( script_dll = LoadLibrary( str_script_dll ) ) == NULL )
    {
        printf( "Script library<%s> load fail.\n", str_script_dll );
        return 0;
    }

    if( !run_func.empty() )
    {
        if( Engine->RegisterGlobalFunction( "void __CompilerLog(string& text)", asFUNCTION( Global_Log ), asCALL_CDECL ) < 0 )
            printf( "Warning: cannot bind masking Log()." );
    }

    BindFunc bind_func;
    if( !( bind_func = (BindFunc) GetProcAddress( script_dll, "Bind" ) ) )
    {
        printf( "Function Bind not found, error<%u>.\n", GetLastError() );
        return 0;
    }
    int result = ( *bind_func )( Engine );
    if( result )
        printf( "Warning, Bind result: %d.\n", result );
    FreeLibrary( script_dll );

    int type_count = Engine->GetObjectTypeCount();
    for( int i = 0; i < type_count; i++ )
    {
        asIObjectType* ot = Engine->GetObjectTypeByIndex( i );
        if( !strcmp( ot->GetName(), "Critter" ) )
            IsServer = true;
        else if( !strcmp( ot->GetName(), "CritterCl" ) )
            IsClient = true;
        else if( !strcmp( ot->GetName(), "MapperMap" ) )
            IsMapper = true;
    }

    /************************************************************************/
    /* Compile                                                              */
    /************************************************************************/

    printf( "Compiling...\n" );

    // Preprocessor
    Preprocessor::VectorOutStream vos;
    Preprocessor::VectorOutStream vos_err;
    Preprocessor::FileSource      fsrc;
    fsrc.CurrentDir = "";
    fsrc.Stream = NULL;

    int pragma_type = PRAGMA_UNKNOWN;
    if( IsServer )
        pragma_type = PRAGMA_SERVER;
    else if( IsClient )
        pragma_type = PRAGMA_CLIENT;
    else if( IsMapper )
        pragma_type = PRAGMA_MAPPER;
    Preprocessor::SetPragmaCallback( new ScriptPragmaCallback( pragma_type ) );

    for( size_t i = 0; i < defines.size(); i++ )
        Preprocessor::Define( string( defines[ i ] ) );
    if( !run_func.empty() )
        Preprocessor::Define( string( "Log __CompilerLog" ) );
    LNT = new Preprocessor::LineNumberTranslator();
    int res = Preprocessor::Preprocess( str_fname, fsrc, vos, true, &vos_err, LNT );
    if( res )
    {
        vos_err.PushNull();
        printf( "Unable to preprocess. Errors:\n%s\n", vos_err.GetData() );
        return 0;
    }

    Buf = new char[ vos.GetSize() + 1 ];
    memcpy( Buf, vos.GetData(), vos.GetSize() );
    Buf[ vos.GetSize() ] = '\0';

    if( str_prep )
    {
        FILE* f = NULL;
        if( !fopen_s( &f, str_prep, "wt" ) )
        {
            Preprocessor::VectorOutStream vos_formatted;
            vos_formatted << string( vos.GetData(), vos.GetSize() );
            vos_formatted.Format();
            char* buf_formatted = new char[ vos_formatted.GetSize() + 1 ];
            memcpy( buf_formatted, vos_formatted.GetData(), vos_formatted.GetSize() );
            buf_formatted[ vos_formatted.GetSize() ] = '\0';
            fwrite( buf_formatted, sizeof( char ), strlen( buf_formatted ), f );
            fclose( f );
            delete buf_formatted;
        }
        else
        {
            printf( "Unable to create preprocessed file<%s>.\n", str_prep );
        }
    }

    // Break buffer into null-terminated lines
    for( int i = 0; Buf[ i ] != '\0'; i++ )
        if( Buf[ i ] == '\n' )
            Buf[ i ] = '\0';

    // Compiler
    __int64 freq, fp, fp2;
    QueryPerformanceFrequency( (PLARGE_INTEGER) &freq );
    QueryPerformanceCounter( (PLARGE_INTEGER) &fp );

    asIScriptModule* module = Engine->GetModule( 0, asGM_ALWAYS_CREATE );
    if( !module )
    {
        printf( "Can't create module.\n" );
        return 0;
    }

    if( module->AddScriptSection( NULL, vos.GetData(), vos.GetSize(), 0 ) < 0 )
    {
        printf( "Unable to add section.\n" );
        return 0;
    }

    if( module->Build() < 0 )
    {
        printf( "Unable to build.\n" );
        return 0;
    }

    // Check global not allowed types, only for server
    if( IsServer )
    {
        int bad_typeids[] =
        {
            Engine->GetTypeIdByDecl( "Critter@" ),
            Engine->GetTypeIdByDecl( "Critter@[]" ),
            Engine->GetTypeIdByDecl( "Item@" ),
            Engine->GetTypeIdByDecl( "Item@[]" ),
            Engine->GetTypeIdByDecl( "Map@" ),
            Engine->GetTypeIdByDecl( "Map@[]" ),
            Engine->GetTypeIdByDecl( "Location@" ),
            Engine->GetTypeIdByDecl( "Location@[]" ),
            Engine->GetTypeIdByDecl( "GameVar@" ),
            Engine->GetTypeIdByDecl( "GameVar@[]" ),
            Engine->GetTypeIdByDecl( "CraftItem@" ),
            Engine->GetTypeIdByDecl( "CraftItem@[]" ),
        };
        int bad_typeids_count = sizeof( bad_typeids ) / sizeof( int );
        for( int k = 0; k < bad_typeids_count; k++ )
            bad_typeids[ k ] &= asTYPEID_MASK_SEQNBR;

        vector< int > bad_typeids_class;
        for( int m = 0, n = module->GetObjectTypeCount(); m < n; m++ )
        {
            asIObjectType* ot = module->GetObjectTypeByIndex( m );
            for( int i = 0, j = ot->GetPropertyCount(); i < j; i++ )
            {
                int type = 0;
                ot->GetProperty( i, NULL, &type, NULL, NULL );
                type &= asTYPEID_MASK_SEQNBR;
                for( int k = 0; k < bad_typeids_count; k++ )
                {
                    if( type == bad_typeids[ k ] )
                    {
                        bad_typeids_class.push_back( ot->GetTypeId() & asTYPEID_MASK_SEQNBR );
                        break;
                    }
                }
            }
        }

        bool g_fail = false;
        bool g_fail_class = false;
        for( int i = 0, j = module->GetGlobalVarCount(); i < j; i++ )
        {
            int type = 0;
            module->GetGlobalVar( i, NULL, &type, NULL );

            while( type & asTYPEID_TEMPLATE )
            {
                asIObjectType* obj = (asIObjectType*) Engine->GetObjectTypeById( type );
                if( !obj )
                    break;
                type = obj->GetSubTypeId();
            }

            type &= asTYPEID_MASK_SEQNBR;

            for( int k = 0; k < bad_typeids_count; k++ )
            {
                if( type == bad_typeids[ k ] )
                {
                    const char* name = NULL;
                    module->GetGlobalVar( i, &name, NULL, NULL );
                    string      msg = "The global variable '" + string( name ) + "' uses a type that cannot be stored globally";
                    Engine->WriteMessage( "", 0, 0, asMSGTYPE_ERROR, msg.c_str() );
                    g_fail = true;
                    break;
                }
            }
            if( std::find( bad_typeids_class.begin(), bad_typeids_class.end(), type ) != bad_typeids_class.end() )
            {
                const char* name = NULL;
                module->GetGlobalVar( i, &name, NULL, NULL );
                string      msg = "The global variable '" + string( name ) + "' uses a type in class property that cannot be stored globally";
                Engine->WriteMessage( "", 0, 0, asMSGTYPE_ERROR, msg.c_str() );
                g_fail_class = true;
            }
        }

        if( g_fail || g_fail_class )
        {
            if( !g_fail_class )
                printf( "Erase global variable listed above.\n" );
            else
                printf( "Erase global variable or class property listed above.\n" );
            printf( "Classes that cannot be stored in global scope: Critter, Item, ProtoItem, Map, Location, GlobalVar.\n" );
            printf( "Hint: store their Ids, instead of pointers.\n" );
            return 0;
        }
    }

    QueryPerformanceCounter( (PLARGE_INTEGER) &fp2 );
    printf( "Success.\nTime: %.02f ms.\n", double( ( (double) fp2 - (double) fp ) / (double) freq * 1000 ) /*,t2-t1*/ );

    for( size_t i = 0; i < run_func.size(); i++ )
        RunMain( module, run_func[ i ] );

    Engine->Release();
    FreeLibrary( comp_dll );
    if( Buf )
        delete Buf;
    Buf = NULL;
    if( LNT )
        delete LNT;
    /************************************************************************/
    /*                                                                      */
    /************************************************************************/
    return 0;
}
