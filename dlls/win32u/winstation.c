/*
 * Window stations and desktops
 *
 * Copyright 2002 Alexandre Julliard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#if 0
#pragma makedep unix
#endif

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include <stdarg.h>
#include "windef.h"
#include "winbase.h"
#include "ntuser.h"
#include "ddk/wdm.h"
#include "ntgdi_private.h"
#include "ntuser_private.h"
#include "wine/server.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(winstation);
WINE_DECLARE_DEBUG_CHANNEL(win);


#define DESKTOP_ALL_ACCESS 0x01ff

BOOL is_virtual_desktop(void)
{
    const desktop_shm_t *desktop = get_desktop_shared_memory();
    unsigned int flags;

    if (!desktop) return FALSE;
    SHARED_READ_BEGIN( desktop, desktop_shm_t )
    {
        flags = desktop->flags;
    }
    SHARED_READ_END

    return !!(flags & DF_WINE_CREATE_DESKTOP);
}

/***********************************************************************
 *           NtUserCreateWindowStation  (win32u.@)
 */
HWINSTA WINAPI NtUserCreateWindowStation( OBJECT_ATTRIBUTES *attr, ACCESS_MASK access, ULONG arg3,
                                          ULONG arg4, ULONG arg5, ULONG arg6, ULONG arg7 )
{
    HANDLE ret;

    if (attr->ObjectName->Length >= MAX_PATH * sizeof(WCHAR))
    {
        RtlSetLastWin32Error( ERROR_FILENAME_EXCED_RANGE );
        return 0;
    }

    SERVER_START_REQ( create_winstation )
    {
        req->flags      = 0;
        req->access     = access;
        req->attributes = attr->Attributes;
        req->rootdir    = wine_server_obj_handle( attr->RootDirectory );
        wine_server_add_data( req, attr->ObjectName->Buffer, attr->ObjectName->Length );
        wine_server_call_err( req );
        ret = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;
    return ret;
}

/******************************************************************************
 *           NtUserOpenWindowStation  (win32u.@)
 */
HWINSTA WINAPI NtUserOpenWindowStation( OBJECT_ATTRIBUTES *attr, ACCESS_MASK access )
{
    HANDLE ret = 0;

    SERVER_START_REQ( open_winstation )
    {
        req->access     = access;
        req->attributes = attr->Attributes;
        req->rootdir    = wine_server_obj_handle( attr->RootDirectory );
        wine_server_add_data( req, attr->ObjectName->Buffer, attr->ObjectName->Length );
        if (!wine_server_call_err( req )) ret = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;
    return ret;
}

/***********************************************************************
 *           NtUserCloseWindowStation  (win32u.@)
 */
BOOL WINAPI NtUserCloseWindowStation( HWINSTA handle )
{
    BOOL ret;
    SERVER_START_REQ( close_winstation )
    {
        req->handle = wine_server_obj_handle( handle );
        ret = !wine_server_call_err( req );
    }
    SERVER_END_REQ;
    return ret;
}

/***********************************************************************
 *           NtUSerGetProcessWindowStation  (win32u.@)
 */
HWINSTA WINAPI NtUserGetProcessWindowStation(void)
{
    HWINSTA ret = 0;

    SERVER_START_REQ( get_process_winstation )
    {
        if (!wine_server_call_err( req ))
            ret = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;
    return ret;
}

/***********************************************************************
 *           NtUserSetProcessWindowStation  (win32u.@)
 */
BOOL WINAPI NtUserSetProcessWindowStation( HWINSTA handle )
{
    BOOL ret;

    SERVER_START_REQ( set_process_winstation )
    {
        req->handle = wine_server_obj_handle( handle );
        ret = !wine_server_call_err( req );
    }
    SERVER_END_REQ;
    return ret;
}

/***********************************************************************
 *           NtUserCreateDesktopEx   (win32u.@)
 */
HDESK WINAPI NtUserCreateDesktopEx( OBJECT_ATTRIBUTES *attr, UNICODE_STRING *device,
                                    DEVMODEW *devmode, DWORD flags, ACCESS_MASK access,
                                    ULONG heap_size )
{
    WCHAR buffer[MAX_PATH];
    HANDLE ret;

    if ((device && device->Length) || (devmode && !(flags & DF_WINE_CREATE_DESKTOP)))
    {
        RtlSetLastWin32Error( ERROR_INVALID_PARAMETER );
        return 0;
    }
    if (attr->ObjectName->Length >= MAX_PATH * sizeof(WCHAR))
    {
        RtlSetLastWin32Error( ERROR_FILENAME_EXCED_RANGE );
        return 0;
    }
    SERVER_START_REQ( create_desktop )
    {
        req->flags      = flags;
        req->access     = access;
        req->attributes = attr->Attributes;
        wine_server_add_data( req, attr->ObjectName->Buffer, attr->ObjectName->Length );
        wine_server_call_err( req );
        ret = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;
    if (!devmode) return ret;

    lstrcpynW( buffer, attr->ObjectName->Buffer, attr->ObjectName->Length / sizeof(WCHAR) + 1 );
    if (!user_driver->pCreateDesktop( buffer, devmode->dmPelsWidth, devmode->dmPelsHeight ))
    {
        NtUserCloseDesktop( ret );
        return 0;
    }

    /* force update display cache to use virtual desktop display settings */
    if (flags & DF_WINE_CREATE_DESKTOP) update_display_cache( TRUE, TRUE );
    return ret;
}

/***********************************************************************
 *           NtUserOpenDesktop   (win32u.@)
 */
HDESK WINAPI NtUserOpenDesktop( OBJECT_ATTRIBUTES *attr, DWORD flags, ACCESS_MASK access )
{
    HANDLE ret = 0;
    if (attr->ObjectName->Length >= MAX_PATH * sizeof(WCHAR))
    {
        RtlSetLastWin32Error( ERROR_FILENAME_EXCED_RANGE );
        return 0;
    }
    SERVER_START_REQ( open_desktop )
    {
        req->winsta     = wine_server_obj_handle( attr->RootDirectory );
        req->flags      = flags;
        req->access     = access;
        req->attributes = attr->Attributes;
        wine_server_add_data( req, attr->ObjectName->Buffer, attr->ObjectName->Length );
        if (!wine_server_call_err( req )) ret = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;
    return ret;
 }

/***********************************************************************
 *           NtUserCloseDesktop  (win32u.@)
 */
BOOL WINAPI NtUserCloseDesktop( HDESK handle )
{
    BOOL ret;
    SERVER_START_REQ( close_desktop )
    {
        req->handle = wine_server_obj_handle( handle );
        ret = !wine_server_call_err( req );
    }
    SERVER_END_REQ;
    return ret;
}

/***********************************************************************
 *           NtUserGetThreadDesktop   (win32u.@)
 */
HDESK WINAPI NtUserGetThreadDesktop( DWORD thread )
{
    HDESK ret = 0;

    SERVER_START_REQ( get_thread_desktop )
    {
        req->tid = thread;
        if (!wine_server_call_err( req )) ret = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;
    return ret;
}

/***********************************************************************
 *           NtUserSetThreadDesktop   (win32u.@)
 */
BOOL WINAPI NtUserSetThreadDesktop( HDESK handle )
{
    BOOL ret, was_virtual_desktop = is_virtual_desktop();

    SERVER_START_REQ( set_thread_desktop )
    {
        req->handle = wine_server_obj_handle( handle );
        ret = !wine_server_call_err( req );
    }
    SERVER_END_REQ;

    if (ret)  /* reset the desktop windows */
    {
        struct user_thread_info *thread_info = get_user_thread_info();
        thread_info->client_info.top_window = 0;
        thread_info->client_info.msg_window = 0;
        if (was_virtual_desktop != is_virtual_desktop()) update_display_cache( TRUE, TRUE );
        if (thread_info->desktop_shm)
        {
            NtUnmapViewOfSection( GetCurrentProcess(), (void *)thread_info->desktop_shm );
            thread_info->desktop_shm = NULL;
        }
    }
    return ret;
}

/***********************************************************************
 *           NtUserOpenInputDesktop   (win32u.@)
 */
HDESK WINAPI NtUserOpenInputDesktop( DWORD flags, BOOL inherit, ACCESS_MASK access )
{
    HANDLE ret = 0;

    TRACE( "(%x,%i,%x)\n", (int)flags, inherit, (int)access );

    if (flags)
        FIXME( "partial stub flags %08x\n", (int)flags );

    SERVER_START_REQ( open_input_desktop )
    {
        req->flags      = flags;
        req->access     = access;
        req->attributes = inherit ? OBJ_INHERIT : 0;
        if (!wine_server_call_err( req )) ret = wine_server_ptr_handle( reply->handle );
    }
    SERVER_END_REQ;

    return ret;
}

/***********************************************************************
 *           NtUserGetObjectInformation   (win32u.@)
 */
BOOL WINAPI NtUserGetObjectInformation( HANDLE handle, INT index, void *info,
                                        DWORD len, DWORD *needed )
{
    BOOL ret;

    static const WCHAR desktopW[] = {'D','e','s','k','t','o','p',0};
    static const WCHAR window_stationW[] = {'W','i','n','d','o','w','S','t','a','t','i','o','n',0};

    switch(index)
    {
    case UOI_FLAGS:
        {
            USEROBJECTFLAGS *obj_flags = info;
            if (needed) *needed = sizeof(*obj_flags);
            if (len < sizeof(*obj_flags))
            {
                RtlSetLastWin32Error( ERROR_BUFFER_OVERFLOW );
                return FALSE;
            }
            SERVER_START_REQ( set_user_object_info )
            {
                req->handle = wine_server_obj_handle( handle );
                req->flags  = 0;
                ret = !wine_server_call_err( req );
                if (ret)
                {
                    /* FIXME: inherit flag */
                    obj_flags->dwFlags = reply->old_obj_flags;
                }
            }
            SERVER_END_REQ;
        }
        return ret;

    case UOI_TYPE:
        SERVER_START_REQ( set_user_object_info )
        {
            req->handle = wine_server_obj_handle( handle );
            req->flags  = 0;
            ret = !wine_server_call_err( req );
            if (ret)
            {
                size_t size = reply->is_desktop ? sizeof(desktopW) : sizeof(window_stationW);
                if (needed) *needed = size;
                if (len < size)
                {
                    RtlSetLastWin32Error( ERROR_INSUFFICIENT_BUFFER );
                    ret = FALSE;
                }
                else memcpy( info, reply->is_desktop ? desktopW : window_stationW, size );
            }
        }
        SERVER_END_REQ;
        return ret;

    case UOI_NAME:
        {
            WCHAR buffer[MAX_PATH];
            SERVER_START_REQ( set_user_object_info )
            {
                req->handle = wine_server_obj_handle( handle );
                req->flags  = 0;
                wine_server_set_reply( req, buffer, sizeof(buffer) - sizeof(WCHAR) );
                ret = !wine_server_call_err( req );
                if (ret)
                {
                    size_t size = wine_server_reply_size( reply );
                    buffer[size / sizeof(WCHAR)] = 0;
                    size += sizeof(WCHAR);
                    if (needed) *needed = size;
                    if (len < size)
                    {
                        RtlSetLastWin32Error( ERROR_INSUFFICIENT_BUFFER );
                        ret = FALSE;
                    }
                    else memcpy( info, buffer, size );
                }
            }
            SERVER_END_REQ;
        }
        return ret;

    case UOI_USER_SID:
        FIXME( "not supported index %d\n", index );
        /* fall through */
    default:
        RtlSetLastWin32Error( ERROR_INVALID_PARAMETER );
        return FALSE;
    }
}

#define TICKSPERSEC  10000000

/***********************************************************************
 *           NtUserSetObjectInformation   (win32u.@)
 */
BOOL WINAPI NtUserSetObjectInformation( HANDLE handle, INT index, void *info, DWORD len )
{
    BOOL ret;
    const USEROBJECTFLAGS *obj_flags = info;
    LONG64 close_timeout = 0;

    if (index == 1000)
    {
        /* Wine specific: set desktop close timeout. */
        if (!info || len < sizeof(DWORD))
        {
            RtlSetLastWin32Error( ERROR_INVALID_PARAMETER );
            return FALSE;
        }
        close_timeout = -(*(DWORD *)info * (ULONG64)TICKSPERSEC / 1000);
    }
    else if (index != UOI_FLAGS || !info || len < sizeof(*obj_flags))
    {
        RtlSetLastWin32Error( ERROR_INVALID_PARAMETER );
        return FALSE;
    }
    /* FIXME: inherit flag */
    SERVER_START_REQ( set_user_object_info )
    {
        req->handle    = wine_server_obj_handle( handle );
        if (index == 1000)
        {
            req->flags = SET_USER_OBJECT_SET_CLOSE_TIMEOUT;
            req->close_timeout = close_timeout;
        }
        else
        {
            req->flags     = SET_USER_OBJECT_SET_FLAGS;
            req->obj_flags = obj_flags->dwFlags;
        }
        ret = !wine_server_call_err( req );
    }
    SERVER_END_REQ;
    return ret;
}

#ifdef _WIN64
static inline TEB64 *NtCurrentTeb64(void) { return NULL; }
#else
static inline TEB64 *NtCurrentTeb64(void) { return (TEB64 *)NtCurrentTeb()->GdiBatchCount; }
#endif

HWND get_desktop_window(void)
{
    static const WCHAR wine_service_station_name[] =
        {'_','_','w','i','n','e','s','e','r','v','i','c','e','_','w','i','n','s','t','a','t','i','o','n',0};
    struct ntuser_thread_info *thread_info = NtUserGetThreadInfo();
    WCHAR name[MAX_PATH];
    BOOL is_service;

    if (thread_info->top_window) return UlongToHandle( thread_info->top_window );

    /* don't create an actual explorer desktop window for services */
    if (NtUserGetObjectInformation( NtUserGetProcessWindowStation(), UOI_NAME, name, sizeof(name), NULL )
        && !wcscmp( name, wine_service_station_name ))
        is_service = TRUE;
    else
        is_service = FALSE;

    SERVER_START_REQ( get_desktop_window )
    {
        req->force = is_service;
        if (!wine_server_call( req ))
        {
            thread_info->top_window = reply->top_window;
            thread_info->msg_window = reply->msg_window;
        }
    }
    SERVER_END_REQ;

    if (!thread_info->top_window)
    {
        static const WCHAR appnameW[] = {'\\','?','?','\\','C',':','\\','w','i','n','d','o','w','s',
            '\\','s','y','s','t','e','m','3','2','\\','e','x','p','l','o','r','e','r','.','e','x','e',0};
        static const WCHAR cmdlineW[] = {'"','C',':','\\','w','i','n','d','o','w','s','\\',
            's','y','s','t','e','m','3','2','\\','e','x','p','l','o','r','e','r','.','e','x','e','"',
            ' ','/','d','e','s','k','t','o','p',0};
        static const WCHAR system_dir[] = {'C',':','\\','w','i','n','d','o','w','s','\\',
            's','y','s','t','e','m','3','2','\\',0};
        RTL_USER_PROCESS_PARAMETERS params = { sizeof(params), sizeof(params) };
        ULONG_PTR buffer[offsetof( PS_ATTRIBUTE_LIST, Attributes[2] ) / sizeof(ULONG_PTR)];
        PS_ATTRIBUTE_LIST *ps_attr = (PS_ATTRIBUTE_LIST *)buffer;
        PS_CREATE_INFO create_info;
        WCHAR desktop[MAX_PATH];
        PEB *peb = NtCurrentTeb()->Peb;
        HANDLE process, thread;
        unsigned int status;

        SERVER_START_REQ( set_user_object_info )
        {
            req->handle = wine_server_obj_handle( NtUserGetThreadDesktop(GetCurrentThreadId()) );
            req->flags  = SET_USER_OBJECT_GET_FULL_NAME;
            wine_server_set_reply( req, desktop, sizeof(desktop) - sizeof(WCHAR) );
            if (!wine_server_call( req ))
            {
                size_t size = wine_server_reply_size( reply );
                desktop[size / sizeof(WCHAR)] = 0;
                TRACE( "starting explorer for desktop %s\n", debugstr_w(desktop) );
            }
            else
                desktop[0] = 0;
        }
        SERVER_END_REQ;

        params.Flags           = PROCESS_PARAMS_FLAG_NORMALIZED;
        params.Environment     = peb->ProcessParameters->Environment;
        params.EnvironmentSize = peb->ProcessParameters->EnvironmentSize;
        params.hStdError       = peb->ProcessParameters->hStdError;
        RtlInitUnicodeString( &params.CurrentDirectory.DosPath, system_dir );
        RtlInitUnicodeString( &params.ImagePathName, appnameW + 4 );
        RtlInitUnicodeString( &params.CommandLine, cmdlineW );
        RtlInitUnicodeString( &params.WindowTitle, appnameW + 4 );
        RtlInitUnicodeString( &params.Desktop, desktop );

        ps_attr->Attributes[0].Attribute    = PS_ATTRIBUTE_IMAGE_NAME;
        ps_attr->Attributes[0].Size         = sizeof(appnameW) - sizeof(WCHAR);
        ps_attr->Attributes[0].ValuePtr     = (WCHAR *)appnameW;
        ps_attr->Attributes[0].ReturnLength = NULL;

        ps_attr->Attributes[1].Attribute    = PS_ATTRIBUTE_TOKEN;
        ps_attr->Attributes[1].Size         = sizeof(HANDLE);
        ps_attr->Attributes[1].ValuePtr     = GetCurrentThreadEffectiveToken();
        ps_attr->Attributes[1].ReturnLength = NULL;

        ps_attr->TotalLength = offsetof( PS_ATTRIBUTE_LIST, Attributes[2] );

        if (NtCurrentTeb64() && !NtCurrentTeb64()->TlsSlots[WOW64_TLS_FILESYSREDIR])
        {
            NtCurrentTeb64()->TlsSlots[WOW64_TLS_FILESYSREDIR] = TRUE;
            status = NtCreateUserProcess( &process, &thread, PROCESS_ALL_ACCESS, THREAD_ALL_ACCESS,
                                          NULL, NULL, 0, THREAD_CREATE_FLAGS_CREATE_SUSPENDED, &params,
                                          &create_info, ps_attr );
            NtCurrentTeb64()->TlsSlots[WOW64_TLS_FILESYSREDIR] = FALSE;
        }
        else
            status = NtCreateUserProcess( &process, &thread, PROCESS_ALL_ACCESS, THREAD_ALL_ACCESS,
                                          NULL, NULL, 0, THREAD_CREATE_FLAGS_CREATE_SUSPENDED, &params,
                                          &create_info, ps_attr );
        if (!status)
        {
            NtResumeThread( thread, NULL );
            TRACE_(win)( "started explorer\n" );
            NtUserWaitForInputIdle( process, 10000, FALSE );
            NtClose( thread );
            NtClose( process );
        }
        else ERR_(win)( "failed to start explorer %x\n", status );

        SERVER_START_REQ( get_desktop_window )
        {
            req->force = 1;
            if (!wine_server_call( req ))
            {
                thread_info->top_window = reply->top_window;
                thread_info->msg_window = reply->msg_window;
            }
        }
        SERVER_END_REQ;
    }

    if (!thread_info->top_window) ERR_(win)( "failed to create desktop window\n" );
    else user_driver->pSetDesktopWindow( UlongToHandle( thread_info->top_window ));

    register_builtin_classes();
    return UlongToHandle( thread_info->top_window );
}

static HANDLE get_winstations_dir_handle(void)
{
    char bufferA[64];
    WCHAR buffer[64];
    UNICODE_STRING str;
    OBJECT_ATTRIBUTES attr;
    NTSTATUS status;
    HANDLE dir;

    sprintf( bufferA, "\\Sessions\\%u\\Windows\\WindowStations", (int)NtCurrentTeb()->Peb->SessionId );
    str.Buffer = buffer;
    str.MaximumLength = asciiz_to_unicode( buffer, bufferA );
    str.Length = str.MaximumLength - sizeof(WCHAR);
    InitializeObjectAttributes( &attr, &str, 0, 0, NULL );
    status = NtOpenDirectoryObject( &dir, DIRECTORY_CREATE_OBJECT | DIRECTORY_TRAVERSE, &attr );
    return status ? 0 : dir;
}

/***********************************************************************
 *           get_default_desktop
 *
 * Get the name of the desktop to use for this app if not specified explicitly.
 */
static const WCHAR *get_default_desktop( void *buf, size_t buf_size )
{
    const WCHAR *p, *appname = NtCurrentTeb()->Peb->ProcessParameters->ImagePathName.Buffer;
    KEY_VALUE_PARTIAL_INFORMATION *info = buf;
    WCHAR *buffer = buf;
    HKEY tmpkey, appkey;
    DWORD len;

    static const WCHAR defaultW[] = {'D','e','f','a','u','l','t',0};

    if ((p = wcsrchr( appname, '/' ))) appname = p + 1;
    if ((p = wcsrchr( appname, '\\' ))) appname = p + 1;
    len = lstrlenW(appname);
    if (len > MAX_PATH) return defaultW;
    memcpy( buffer, appname, len * sizeof(WCHAR) );
    asciiz_to_unicode( buffer + len, "\\Explorer" );

    /* @@ Wine registry key: HKCU\Software\Wine\AppDefaults\app.exe\Explorer */
    if ((tmpkey = reg_open_hkcu_key( "Software\\Wine\\AppDefaults" )))
    {
        appkey = reg_open_key( tmpkey, buffer, lstrlenW(buffer) * sizeof(WCHAR) );
        NtClose( tmpkey );
        if (appkey)
        {
            len = query_reg_ascii_value( appkey, "Desktop", info, buf_size );
            NtClose( appkey );
            if (len) return (const WCHAR *)info->Data;
        }
    }

    /* @@ Wine registry key: HKCU\Software\Wine\Explorer */
    if ((appkey = reg_open_hkcu_key( "Software\\Wine\\Explorer" )))
    {
        len = query_reg_ascii_value( appkey, "Desktop", info, buf_size );
        NtClose( appkey );
        if (len) return (const WCHAR *)info->Data;
    }

    return defaultW;
}

static volatile void *map_shared_memory_section( const WCHAR *name, SIZE_T size, HANDLE root )
{
    OBJECT_ATTRIBUTES attr;
    UNICODE_STRING section_str;
    HANDLE handle;
    UINT status;
    void *ptr;

    RtlInitUnicodeString( &section_str, name );
    InitializeObjectAttributes( &attr, &section_str, 0, root, NULL );
    if (!(status = NtOpenSection( &handle, SECTION_ALL_ACCESS, &attr )))
    {
        ptr = NULL;
        status = NtMapViewOfSection( handle, GetCurrentProcess(), &ptr, 0, 0, NULL,
                                     &size, ViewUnmap, 0, PAGE_READONLY );
        NtClose( handle );
    }

    if (status)
    {
        WARN( "Failed to map view of section %s, status %#x\n", debugstr_w(name), status );
        return NULL;
    }

    return ptr;
}

volatile struct global_shared_memory *get_global_shared_memory( void )
{
    static const WCHAR global_mappingW[] =
    {
        '\\','?','?','\\','_','_','w','i','n','e','_','w','i','n','3','2','u','_','m','a','p','p','i','n','g',0
    };
    static struct global_shared_memory *global_shared;
    struct global_shared_memory *ret;
    UNICODE_STRING section_str;
    OBJECT_ATTRIBUTES attr;
    LARGE_INTEGER size_l;
    unsigned int status;
    HANDLE handle;
    SIZE_T size;

    __WINE_ATOMIC_LOAD_RELAXED( &global_shared, &ret );
    if (ret) return ret;

    RtlInitUnicodeString( &section_str, global_mappingW );
    InitializeObjectAttributes( &attr, &section_str, OBJ_CASE_INSENSITIVE | OBJ_OPENIF | OBJ_PERMANENT, NULL, NULL );
    size_l.QuadPart = sizeof(struct global_shared_memory);
    status = NtCreateSection( &handle, SECTION_ALL_ACCESS, &attr, &size_l, PAGE_READWRITE, SEC_COMMIT, NULL );
    if (status && status != STATUS_OBJECT_NAME_EXISTS)
    {
        static int once;
        if (!once++)
            ERR( "Failed to get global shared memory, status %#x.\n", status );
    }
    size = sizeof(struct global_shared_memory);
    status = NtMapViewOfSection( handle, GetCurrentProcess(), (void **)&ret, 0, 0, NULL,
                                 &size, ViewUnmap, 0, PAGE_READWRITE );
    NtClose( handle );
    if (status)
    {
        ERR( "failed to map view of section, status %#x\n", status );
        return NULL;
    }
    if (InterlockedCompareExchangePointer( (void **)&global_shared, ret, NULL ))
    {
        if (NtUnmapViewOfSection( GetCurrentProcess(), ret ))
            ERR( "NtUnmapViewOfSection failed.\n" );
        ret = global_shared;
    }

    return ret;
}

const desktop_shm_t *get_desktop_shared_memory(void)
{
    static const WCHAR dir_desktop_maps[] =
    {
        '_','_','w','i','n','e','_','d','e','s','k','t','o','p','_','m','a','p','p','i','n','g','s','\\',0
    };
    struct user_thread_info *thread_info = get_user_thread_info();
    HANDLE root, handles[2];
    WCHAR buf[MAX_PATH], *ptr;
    DWORD i, needed;

    if (thread_info->desktop_shm) return thread_info->desktop_shm;

    handles[0] = NtUserGetProcessWindowStation();
    if (!handles[0]) return NULL;

    handles[1] = NtUserGetThreadDesktop( GetCurrentThreadId() );
    if (!handles[1]) return NULL;

    memcpy( buf, dir_desktop_maps, wcslen(dir_desktop_maps) * sizeof(WCHAR) );
    ptr = buf + wcslen(dir_desktop_maps);

    for (i = 0; i < 2; i++)
    {
        NtUserGetObjectInformation( handles[i], UOI_NAME, (void *)ptr, sizeof(buf) - (ptr - buf) * sizeof(WCHAR), &needed );
        ptr += needed / sizeof(WCHAR);
        if (i == 0) *(ptr - 1) = '\\';
    }

    root = get_winstations_dir_handle();
    thread_info->desktop_shm = map_shared_memory_section( buf, sizeof(*thread_info->desktop_shm), root );
    NtClose( root );

    return thread_info->desktop_shm;
}

const queue_shm_t *get_queue_shared_memory(void)
{
    struct user_thread_info *thread_info = get_user_thread_info();
    UINT tid = GetCurrentThreadId();
    WCHAR bufferW[MAX_PATH];
    char buffer[MAX_PATH];

    if (thread_info->queue_shm) return thread_info->queue_shm;

    snprintf( buffer, ARRAY_SIZE(buffer), "\\KernelObjects\\__wine_thread_mappings\\%08x-queue", tid );
    asciiz_to_unicode( bufferW, buffer );
    thread_info->queue_shm = map_shared_memory_section( bufferW, sizeof(*thread_info->queue_shm), NULL );
    return thread_info->queue_shm;
}

static const input_shm_t *get_thread_input_shared_memory( UINT tid, const input_shm_t *input_shm )
{
    WCHAR bufferW[MAX_PATH];
    char buffer[MAX_PATH];

    if (input_shm && input_shm->tid == tid) return input_shm;
    if (input_shm) NtUnmapViewOfSection( GetCurrentProcess(), (void *)input_shm );

    snprintf( buffer, ARRAY_SIZE(buffer), "\\KernelObjects\\__wine_thread_mappings\\%08x-input", tid );
    asciiz_to_unicode( bufferW, buffer );
    return map_shared_memory_section( bufferW, sizeof(*input_shm), NULL );
}

const input_shm_t *get_input_shared_memory(void)
{
    const queue_shm_t *queue = get_queue_shared_memory();
    struct user_thread_info *thread_info = get_user_thread_info();
    UINT tid;

    if (!queue) return NULL;
    SHARED_READ_BEGIN( queue, queue_shm_t )
    {
        tid = queue->input_tid;
    }
    SHARED_READ_END

    thread_info->input_shm = get_thread_input_shared_memory( tid, thread_info->input_shm );
    return thread_info->input_shm;
}

const input_shm_t *get_foreground_shared_memory(void)
{
    const desktop_shm_t *desktop = get_desktop_shared_memory();
    struct user_thread_info *thread_info = get_user_thread_info();
    UINT tid;

    if (!desktop) return NULL;
    SHARED_READ_BEGIN( desktop, desktop_shm_t )
    {
        tid = desktop->foreground_tid;
    }
    SHARED_READ_END

    if (!tid) return NULL;
    thread_info->foreground_shm = get_thread_input_shared_memory( tid, thread_info->foreground_shm );
    return thread_info->foreground_shm;
}

/***********************************************************************
 *           winstation_init
 *
 * Connect to the process window station and desktop.
 */
void winstation_init(void)
{
    RTL_USER_PROCESS_PARAMETERS *params = NtCurrentTeb()->Peb->ProcessParameters;
    WCHAR *winstation = NULL, *desktop = NULL, *buffer = NULL;
    HANDLE handle, dir = NULL;
    OBJECT_ATTRIBUTES attr;
    UNICODE_STRING str;

    static const WCHAR winsta0[] = {'W','i','n','S','t','a','0',0};

    if (params->Desktop.Length)
    {
        buffer = malloc( params->Desktop.Length + sizeof(WCHAR) );
        memcpy( buffer, params->Desktop.Buffer, params->Desktop.Length );
        buffer[params->Desktop.Length / sizeof(WCHAR)] = 0;
        if ((desktop = wcschr( buffer, '\\' )))
        {
            *desktop++ = 0;
            winstation = buffer;
        }
        else desktop = buffer;
    }

    /* set winstation if explicitly specified, or if we don't have one yet */
    if (buffer || !NtUserGetProcessWindowStation())
    {
        str.Buffer = (WCHAR *)(winstation ? winstation : winsta0);
        str.Length = str.MaximumLength = lstrlenW( str.Buffer ) * sizeof(WCHAR);
        dir = get_winstations_dir_handle();
        InitializeObjectAttributes( &attr, &str, OBJ_CASE_INSENSITIVE | OBJ_OPENIF,
                                    dir, NULL );

        handle = NtUserCreateWindowStation( &attr, STANDARD_RIGHTS_REQUIRED | WINSTA_ALL_ACCESS, 0, 0, 0, 0, 0 );
        if (handle)
        {
            NtUserSetProcessWindowStation( handle );
            /* only WinSta0 is visible */
            if (!winstation || !wcsicmp( winstation, winsta0 ))
            {
                USEROBJECTFLAGS flags;
                flags.fInherit  = FALSE;
                flags.fReserved = FALSE;
                flags.dwFlags   = WSF_VISIBLE;
                NtUserSetObjectInformation( handle, UOI_FLAGS, &flags, sizeof(flags) );
            }
        }
    }
    if (buffer || !NtUserGetThreadDesktop( GetCurrentThreadId() ))
    {
        char buffer[4096];
        str.Buffer = (WCHAR *)(desktop ? desktop : get_default_desktop( buffer, sizeof(buffer) ));
        str.Length = str.MaximumLength = lstrlenW( str.Buffer ) * sizeof(WCHAR);
        if (!dir) dir = get_winstations_dir_handle();
        InitializeObjectAttributes( &attr, &str, OBJ_CASE_INSENSITIVE | OBJ_OPENIF,
                                    dir, NULL );

        handle = NtUserCreateDesktopEx( &attr, NULL, NULL, 0, STANDARD_RIGHTS_REQUIRED | DESKTOP_ALL_ACCESS, 0 );
        if (handle) NtUserSetThreadDesktop( handle );
    }
    NtClose( dir );
    free( buffer );
}
