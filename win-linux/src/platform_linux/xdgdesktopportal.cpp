#define _GNU_SOURCE 1
#include "xdgdesktopportal.h"
#include <QVariant>
#include <QHash>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <glib.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <dbus/dbus.h>
#include <sys/syscall.h>
#include <linux/random.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#if defined(__x86_64__)
# define GETRANDOM_NR 318
#elif defined(__i386__)
# define GETRANDOM_NR 355
#elif defined(__arm__)
# define GETRANDOM_NR 384
#elif defined(__ppc64le__)
# define GETRANDOM_NR 359
#elif defined(__PPC64LE__)
# define GETRANDOM_NR 359
#elif defined(__ppc64__)
# define GETRANDOM_NR 359
#elif defined(__PPC64__)
# define GETRANDOM_NR 359
#elif defined(__s390x__)
# define GETRANDOM_NR 349
#elif defined(__s390__)
# define GETRANDOM_NR 349
#endif

#if defined(SYS_getrandom)
# if defined(GETRANDOM_NR)
static_assert(GETRANDOM_NR == SYS_getrandom,
              "GETRANDOM_NR should match the actual SYS_getrandom value");
# endif
#else
# define SYS_getrandom GETRANDOM_NR
#endif

#if defined(GRND_NONBLOCK)
static_assert(GRND_NONBLOCK == 1,
              "If GRND_NONBLOCK is not 1 the #define below is wrong");
#else
# define GRND_NONBLOCK 1
#endif

#define __dbusOpen dbus_message_iter_open_container
#define __dbusClose dbus_message_iter_close_container
#define __dbusAppend dbus_message_iter_append_basic
//#define ADD_EXTENSION // not reccomended


const char URI_PREFIX[] = "file://";
constexpr size_t URI_PREFIX_SIZE = sizeof(URI_PREFIX) - 1;
const char* dbus_unique_name = nullptr;
const char* error_code = nullptr;
DBusConnection* dbus_conn;
DBusError dbus_err;

enum class EntryType : uchar {
    Directory, Multiple
};

extern "C" {
typedef unsigned int uint;
typedef enum {
    SUCCESS,
    ERROR,
    CANCEL
} Result;

typedef struct {
    const char* name;
    const char* pattern;
} FilterItem;

Result initDBus(void);
const char* getErrorText(void);
char* strcopy(const char* start, const char* end, char* out);
void quitDBus(void);
void clearDBusError(void);
void setErrorText(const char* msg);
void Free(void* p);
void freePath(char* filePath);
void pathSetFree(const void* pathSet);
void pathSetFreePath(const char* filePath);
Result pathSetGetCount(const void* pathSet, uint* count);
Result pathSetGetPath(const void* pathSet, uint ind, char** outPath);

} // extern "C"

struct UnrefLater_DBusMessage {
    UnrefLater_DBusMessage(DBusMessage *_msg) noexcept :
        msg(_msg) {}
    ~UnrefLater_DBusMessage() {
        dbus_message_unref(msg);
    }
    DBusMessage *msg;
};

struct FreeLater {
    FreeLater(char* p) noexcept :
        ptr(p) {}
    ~FreeLater() {
        Free(ptr);
    }
    char* ptr;
};

class DBusSignalHandler
{
public:
    DBusSignalHandler() :
        resp_path(nullptr)
    {}
    ~DBusSignalHandler() {
        if (resp_path)
            unsubscribe();
    }

    Result subscribe(const char* unique_path) {
        if (resp_path)
            unsubscribe();
        resp_path = CreateResponsePath(unique_path, dbus_unique_name);
        DBusError err;
        dbus_error_init(&err);
        dbus_bus_add_match(dbus_conn, resp_path, &err);
        if (dbus_error_is_set(&err)) {
            dbus_error_free(&dbus_err);
            dbus_move_error(&err, &dbus_err);
            setErrorText(dbus_err.message);
            return ERROR;
        }
        return SUCCESS;
    }

    void unsubscribe() {
        DBusError err;
        dbus_error_init(&err);
        dbus_bus_remove_match(dbus_conn, resp_path, &err);
        Free(resp_path);
        dbus_error_free(&err);
    }

private:
    char* resp_path;
    static char* CreateResponsePath(const char* unique_path,
                                    const char* unique_name) {
        constexpr const char PART_1[] = "type='signal',sender='org.freedesktop.portal.Desktop',path='";
        constexpr const char PART_2[] = "',interface='org.freedesktop.portal.Request',member='Response',destination='";
        constexpr const char PART_3[] = "'";
        constexpr const char PART_1_SIZE = sizeof(PART_1) - 1;
        constexpr const char PART_2_SIZE = sizeof(PART_2) - 1;
        constexpr const char PART_3_SIZE = sizeof(PART_3) - 1;

        const size_t handle_len = strlen(unique_path);
        const size_t unique_len = strlen(unique_name);
        const size_t len = PART_1_SIZE + handle_len +
                           PART_2_SIZE + unique_len +
                           PART_3_SIZE;
        char* path = (char*)malloc(len + 1);
        char* path_ptr = path;
        path_ptr = strcopy(PART_1, PART_1 + PART_1_SIZE, path_ptr);
        path_ptr = strcopy(unique_path, unique_path + handle_len, path_ptr);
        path_ptr = strcopy(PART_2, PART_2 + PART_2_SIZE, path_ptr);
        path_ptr = strcopy(unique_name, unique_name + unique_len, path_ptr);
        path_ptr = strcopy(PART_3, PART_3 + PART_3_SIZE, path_ptr);
        *path_ptr = '\0';
        return path;
    }
}; // DBusSignalHandler

template <class Fn>
char* replaceSymbol(const char *start, const char *end, char *out, Fn func) {
    for (; start != end; ++start) {
        *out++ = func(*start);
    }
    return out;
}

void setOpenFileEntryType(DBusMessageIter &msg_iter, EntryType entry_type) {
    const char* ENTRY_MULTIPLE = "multiple";
    const char* ENTRY_DIRECTORY = "directory";
    DBusMessageIter iter;
    DBusMessageIter var_iter;
    __dbusOpen(&msg_iter, DBUS_TYPE_DICT_ENTRY, nullptr, &iter);
    __dbusAppend(&iter, DBUS_TYPE_STRING, (entry_type == EntryType::Multiple)
                 ? &ENTRY_MULTIPLE : &ENTRY_DIRECTORY);
    __dbusOpen(&iter, DBUS_TYPE_VARIANT, "b", &var_iter);
    {
        int val = 1;
        __dbusAppend(&var_iter, DBUS_TYPE_BOOLEAN, &val);
    }
    __dbusClose(&iter, &var_iter);
    __dbusClose(&msg_iter, &iter);
}

void setHandleToken(DBusMessageIter &msg_iter, const char *handle_token) {
    const char* HANDLE_TOKEN = "handle_token";
    DBusMessageIter iter;
    DBusMessageIter var_iter;
    __dbusOpen(&msg_iter, DBUS_TYPE_DICT_ENTRY, nullptr, &iter);
    __dbusAppend(&iter, DBUS_TYPE_STRING, &HANDLE_TOKEN);
    __dbusOpen(&iter, DBUS_TYPE_VARIANT, "s", &var_iter);
    __dbusAppend(&var_iter, DBUS_TYPE_STRING, &handle_token);
    __dbusClose(&iter, &var_iter);
    __dbusClose(&msg_iter, &iter);
}

void setFilter(DBusMessageIter &msg_iter, const FilterItem &filterItem) {
    DBusMessageIter struct_iter;
    DBusMessageIter array_iter;
    DBusMessageIter array_struct_iter;
    __dbusOpen(&msg_iter, DBUS_TYPE_STRUCT, nullptr, &struct_iter);
    // add filter name
    __dbusAppend(&struct_iter, DBUS_TYPE_STRING, &filterItem.name);
    // add filter extentions
    __dbusOpen(&struct_iter, DBUS_TYPE_ARRAY, "(us)", &array_iter);
    __dbusOpen(&array_iter, DBUS_TYPE_STRUCT, nullptr, &array_struct_iter);
    {
        const unsigned nil = 0;
        __dbusAppend(&array_struct_iter, DBUS_TYPE_UINT32, &nil);
    }
    __dbusAppend(&array_struct_iter, DBUS_TYPE_STRING, &filterItem.pattern);
    __dbusClose(&array_iter, &array_struct_iter);
    __dbusClose(&struct_iter, &array_iter);
    __dbusClose(&msg_iter, &struct_iter);
}

void setFilters(DBusMessageIter &msg_iter, const FilterItem *filterList,
                uint filterCount, FilterItem *selFilter) {
    if (filterCount != 0) {
        DBusMessageIter dict_iter;
        DBusMessageIter var_iter;
        DBusMessageIter arr_iter;
        const char* FILTERS = "filters";
        const char* CURRENT_FILTER = "current_filter";
        // set filters
        __dbusOpen(&msg_iter, DBUS_TYPE_DICT_ENTRY, nullptr, &dict_iter);
        __dbusAppend(&dict_iter, DBUS_TYPE_STRING, &FILTERS);
        __dbusOpen(&dict_iter, DBUS_TYPE_VARIANT, "a(sa(us))", &var_iter);
        __dbusOpen(&var_iter, DBUS_TYPE_ARRAY, "(sa(us))", &arr_iter);
        for (uint i = 0; i != filterCount; ++i) {
            setFilter(arr_iter, filterList[i]);
        }
        __dbusClose(&var_iter, &arr_iter);
        __dbusClose(&dict_iter, &var_iter);
        __dbusClose(&msg_iter, &dict_iter);

        // set current filter
        __dbusOpen(&msg_iter, DBUS_TYPE_DICT_ENTRY, nullptr, &dict_iter);
        __dbusAppend(&dict_iter, DBUS_TYPE_STRING, &CURRENT_FILTER);
        __dbusOpen(&dict_iter, DBUS_TYPE_VARIANT, "(sa(us))", &var_iter);
        setFilter(var_iter, *selFilter);
        __dbusClose(&dict_iter, &var_iter);
        __dbusClose(&msg_iter, &dict_iter);
    }
}

void setCurrentName(DBusMessageIter &msg_iter, const char *name) {
    if (!name)
        return;
    const char* CURRENT_NAME = "current_name";
    DBusMessageIter dict_iter;
    DBusMessageIter variant_iter;
    __dbusOpen(&msg_iter, DBUS_TYPE_DICT_ENTRY, nullptr, &dict_iter);
    __dbusAppend(&dict_iter, DBUS_TYPE_STRING, &CURRENT_NAME);
    __dbusOpen(&dict_iter, DBUS_TYPE_VARIANT, "s", &variant_iter);
    __dbusAppend(&variant_iter, DBUS_TYPE_STRING, &name);
    __dbusClose(&dict_iter, &variant_iter);
    __dbusClose(&msg_iter, &dict_iter);
}

void setCurrentFolder(DBusMessageIter &msg_iter, const char *path) {
    if (!path)
        return;
    const char* CURRENT_FOLDER = "current_folder";
    DBusMessageIter dict_iter;
    DBusMessageIter var_iter;
    DBusMessageIter arr_iter;
    __dbusOpen(&msg_iter, DBUS_TYPE_DICT_ENTRY, nullptr, &dict_iter);
    __dbusAppend(&dict_iter, DBUS_TYPE_STRING, &CURRENT_FOLDER);
    __dbusOpen(&dict_iter, DBUS_TYPE_VARIANT, "ay", &var_iter);
    __dbusOpen(&var_iter, DBUS_TYPE_ARRAY, "y", &arr_iter);
    // append string as byte array
    const char *p = path;
    do {
        __dbusAppend(&arr_iter, DBUS_TYPE_BYTE, p);
    } while (*p++);
    __dbusClose(&var_iter, &arr_iter);
    __dbusClose(&dict_iter, &var_iter);
    __dbusClose(&msg_iter, &dict_iter);
}

void setCurrentFile(DBusMessageIter &msg_iter, const char *path, const char *name) {
    if (!path || !name)
        return;
    const size_t path_len = strlen(path);
    const size_t name_len = strlen(name);
    char* pathname;
    char* pathname_end;
    size_t pathname_len;
    if (path_len && path[path_len - 1] == '/') {
        pathname_len = path_len + name_len;
        pathname = (char*)malloc(pathname_len + 1);
        pathname_end = pathname;
        pathname_end = strcopy(path, path + path_len, pathname_end);
        pathname_end = strcopy(name, name + name_len, pathname_end);
        *pathname_end++ = '\0';
    } else {
        pathname_len = path_len + 1 + name_len;
        pathname = (char*)malloc(pathname_len + 1);
        pathname_end = pathname;
        pathname_end = strcopy(path, path + path_len, pathname_end);
        *pathname_end++ = '/';
        pathname_end = strcopy(name, name + name_len, pathname_end);
        *pathname_end++ = '\0';
    }
    FreeLater __freeLater(pathname);
    if (access(pathname, F_OK) != 0)
        return;
    const char* CURRENT_FILE = "current_file";
    DBusMessageIter dict_iter;
    DBusMessageIter var_iter;
    DBusMessageIter arr_iter;
    __dbusOpen(&msg_iter, DBUS_TYPE_DICT_ENTRY, nullptr, &dict_iter);
    __dbusAppend(&dict_iter, DBUS_TYPE_STRING, &CURRENT_FILE);
    __dbusOpen(&dict_iter, DBUS_TYPE_VARIANT, "ay", &var_iter);
    __dbusOpen(&var_iter, DBUS_TYPE_ARRAY, "y", &arr_iter);
    // includes the '\0'
    for (const char* p = pathname; p != pathname_end; ++p) {
        __dbusAppend(&arr_iter, DBUS_TYPE_BYTE, p);
    }
    __dbusClose(&var_iter, &arr_iter);
    __dbusClose(&dict_iter, &var_iter);
    __dbusClose(&msg_iter, &dict_iter);
}

Result readDictImpl(const char*, DBusMessageIter&) {
    return SUCCESS;
}

template <class Fn, typename... Args>
Result readDictImpl(const char* key,
                    DBusMessageIter& msg,
                    const char* &candidate_key,
                    Fn& callback,
                    Args&... args) {
    if (strcmp(key, candidate_key) == 0)
        return callback(msg);
    else
        return readDictImpl(key, msg, args...);
}

template <typename... Args>
Result readDict(DBusMessageIter msg, Args... args) {
    if (dbus_message_iter_get_arg_type(&msg) != DBUS_TYPE_ARRAY) {
        setErrorText("D-Bus response is not an array");
        return ERROR;
    }
    DBusMessageIter dict_iter;
    dbus_message_iter_recurse(&msg, &dict_iter);
    while (dbus_message_iter_get_arg_type(&dict_iter) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter iter;
        dbus_message_iter_recurse(&dict_iter, &iter);
        if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING) {
            setErrorText("D-Bus response dict entry does not start with a string");
            return ERROR;
        }
        const char* key;
        dbus_message_iter_get_basic(&iter, &key);
        if (!dbus_message_iter_next(&iter)) {
            setErrorText("D-Bus response dict entry is missing arguments");
            return ERROR;
        }
        if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT) {
            setErrorText("D-Bus response dict entry is not a variant");
            return ERROR;
        }
        DBusMessageIter var_iter;
        dbus_message_iter_recurse(&iter, &var_iter);
        if (readDictImpl(key, var_iter, args...) == ERROR)
            return ERROR;
        if (!dbus_message_iter_next(&dict_iter))
            break;
    }
    return SUCCESS;
}

Result readResponseResults(DBusMessage *msg, DBusMessageIter &resIter) {
    DBusMessageIter iter;
    if (!dbus_message_iter_init(msg, &iter)) {
        setErrorText("D-Bus response is missing arguments");
        return ERROR;
    }
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_UINT32) {
        setErrorText("D-Bus response argument is not a uint32");
        return ERROR;
    }
    dbus_uint32_t resp_code;
    dbus_message_iter_get_basic(&iter, &resp_code);
    if (resp_code != 0) {
        if (resp_code == 1) {
            return CANCEL;
        } else {
            setErrorText("D-Bus file dialog interaction was ended abruptly");
            return ERROR;
        }
    }
    if (!dbus_message_iter_next(&iter)) {
        setErrorText("D-Bus response is missing arguments");
        return ERROR;
    }
    resIter = iter;
    return SUCCESS;
}

Result readResponseUris(DBusMessage* msg, DBusMessageIter& uriIter) {
    DBusMessageIter iter;
    const Result res = readResponseResults(msg, iter);
    if (res != SUCCESS)
        return res;
    bool has_uris = false;
    if (readDict(iter, "uris", [&uriIter, &has_uris](DBusMessageIter &uris_iter) {
            if (dbus_message_iter_get_arg_type(&uris_iter) != DBUS_TYPE_ARRAY) {
                setErrorText("D-Bus response URI is not an array");
                return ERROR;
            }
            dbus_message_iter_recurse(&uris_iter, &uriIter);
            has_uris = true;
            return SUCCESS;
        }) == ERROR)
        return ERROR;

    if (!has_uris) {
        setErrorText("D-Bus response has no URI");
        return ERROR;
    }
    return SUCCESS;
}

void readResponseUrisUnchecked(DBusMessage* msg, DBusMessageIter& uriIter) {
    DBusMessageIter iter;
    dbus_message_iter_init(msg, &iter);
    dbus_message_iter_next(&iter);
    readDict(iter, "uris", [&uriIter](DBusMessageIter& uris_iter) {
        dbus_message_iter_recurse(&uris_iter, &uriIter);
        return SUCCESS;
    });
}

uint readResponseUrisUncheckedSize(DBusMessage* msg) {
    DBusMessageIter iter;
    dbus_message_iter_init(msg, &iter);
    dbus_message_iter_next(&iter);
    uint arr_size = 0;
    readDict(iter, "uris", [&arr_size](DBusMessageIter& uris_iter) {
        //arr_size = dbus_message_iter_get_element_count(&uris_iter);
        // elements count for old D-Bus versions
        if (dbus_message_iter_get_arg_type(&uris_iter) == DBUS_TYPE_ARRAY) {
            DBusMessageIter arr_iter;
            dbus_message_iter_recurse(&uris_iter, &arr_iter);
            while (dbus_message_iter_get_arg_type(&arr_iter) == DBUS_TYPE_STRING) {
                 ++arr_size;
                 if (!dbus_message_iter_next(&arr_iter))
                     break;
            }
        }
        return SUCCESS;
    });
    return arr_size;
}

Result readResponseCurrentFilter(DBusMessage* msg,
                                 FilterItem* selFilter) {
    DBusMessageIter iter;
    const Result res = readResponseResults(msg, iter);
    if (res != SUCCESS)
        return res;
    const char* tmp_curr_filter = nullptr;
    const char* tmp_extn = nullptr;
    if (readDict(iter,
                 "current_filter",
                [&tmp_extn, &tmp_curr_filter](DBusMessageIter& curr_flt_iter) {
                    if (dbus_message_iter_get_arg_type(&curr_flt_iter) != DBUS_TYPE_STRUCT) {
                         // D-Bus response current_filter is not a struct
                         return SUCCESS;
                    }
                    DBusMessageIter curr_flt_struct_iter;
                    dbus_message_iter_recurse(&curr_flt_iter, &curr_flt_struct_iter);
                    if (dbus_message_iter_get_arg_type(&curr_flt_struct_iter) == DBUS_TYPE_STRING) {
                         // Get current filter
                         dbus_message_iter_get_basic(&curr_flt_struct_iter, &tmp_curr_filter);
                    }
                    if (!dbus_message_iter_next(&curr_flt_struct_iter)) {
                         // D-Bus response current_filter struct ended prematurely
                         return SUCCESS;
                    }
                    if (dbus_message_iter_get_arg_type(&curr_flt_struct_iter) != DBUS_TYPE_ARRAY) {
                         // D-Bus response URI is not a string
                         return SUCCESS;
                    }
                    DBusMessageIter curr_flt_arr_iter;
                    dbus_message_iter_recurse(&curr_flt_struct_iter, &curr_flt_arr_iter);
                    if (dbus_message_iter_get_arg_type(&curr_flt_arr_iter) != DBUS_TYPE_STRUCT) {
                         // D-Bus response current_filter is not a struct
                         return SUCCESS;
                    }
                    DBusMessageIter curr_flt_extn_iter;
                    dbus_message_iter_recurse(&curr_flt_arr_iter, &curr_flt_extn_iter);
                    if (dbus_message_iter_get_arg_type(&curr_flt_extn_iter) != DBUS_TYPE_UINT32) {
                         // D-Bus response URI is not a string
                         return SUCCESS;
                    }
                    dbus_uint32_t type;
                    dbus_message_iter_get_basic(&curr_flt_extn_iter, &type);
                    if (type != 0) {
                         // Wrong filter type
                         return SUCCESS;
                    }
                    if (!dbus_message_iter_next(&curr_flt_extn_iter)) {
                         // D-Bus response current_filter struct ended prematurely
                         return SUCCESS;
                    }
                    if (dbus_message_iter_get_arg_type(&curr_flt_extn_iter) != DBUS_TYPE_STRING) {
                         // D-Bus response URI is not a string
                         return SUCCESS;
                    }
                    dbus_message_iter_get_basic(&curr_flt_extn_iter, &tmp_extn);
                    return SUCCESS;
                }) == ERROR)
        return ERROR;

    if (tmp_extn) {
        Free((void*)selFilter->pattern);
        selFilter->pattern = strdup(tmp_extn);
    }
    if (tmp_curr_filter) {
        Free((void*)selFilter->name);
        selFilter->name = strdup(tmp_curr_filter);
    }
    return SUCCESS;
}

Result readResponseUrisSingle(DBusMessage* msg,
                              const char* &file) {
    DBusMessageIter uri_iter;
    const Result res = readResponseUris(msg, uri_iter);
    if (res != SUCCESS)
        return res;
    if (dbus_message_iter_get_arg_type(&uri_iter) != DBUS_TYPE_STRING) {
        setErrorText("D-Bus response URI is not a string");
        return ERROR;
    }
    dbus_message_iter_get_basic(&uri_iter, &file);
    return SUCCESS;
}

#ifdef ADD_EXTENSION
Result readResponseUrisSingleAndCurrentExtension(DBusMessage* msg,
                                                 const char* &file,
                                                 const char* &extn,
                                                 FilterItem* selFilter) {
    DBusMessageIter iter;
    const Result res = readResponseResults(msg, iter);
    if (res != SUCCESS)
        return res;
    const char* tmp_file = nullptr;
    const char* tmp_extn = nullptr;
    const char* tmp_curr_filter = nullptr;
    if (readDict(iter, "uris",
            [&tmp_file](DBusMessageIter& uris_iter) {
                if (dbus_message_iter_get_arg_type(&uris_iter) != DBUS_TYPE_ARRAY) {
                     setErrorText("D-Bus response URI is not an array");
                     return ERROR;
                }
                DBusMessageIter uri_iter;
                dbus_message_iter_recurse(&uris_iter, &uri_iter);
                if (dbus_message_iter_get_arg_type(&uri_iter) != DBUS_TYPE_STRING) {
                     setErrorText("D-Bus response URI is not a string");
                     return ERROR;
                }
                dbus_message_iter_get_basic(&uri_iter, &tmp_file);
                return SUCCESS;
            },
            "current_filter",
            [&tmp_extn, &tmp_curr_filter](DBusMessageIter& curr_flt_iter) {
                if (dbus_message_iter_get_arg_type(&curr_flt_iter) != DBUS_TYPE_STRUCT) {
                     // D-Bus response current_filter is not a struct
                     return SUCCESS;
                }
                DBusMessageIter curr_flt_struct_iter;
                dbus_message_iter_recurse(&curr_flt_iter, &curr_flt_struct_iter);
                if (dbus_message_iter_get_arg_type(&curr_flt_struct_iter) == DBUS_TYPE_STRING) {
                     // Get current filter
                     dbus_message_iter_get_basic(&curr_flt_struct_iter, &tmp_curr_filter);
                }
                if (!dbus_message_iter_next(&curr_flt_struct_iter)) {
                     // D-Bus response current_filter struct ended prematurely
                     return SUCCESS;
                }
                if (dbus_message_iter_get_arg_type(&curr_flt_struct_iter) != DBUS_TYPE_ARRAY) {
                     // D-Bus response URI is not a string
                     return SUCCESS;
                }
                DBusMessageIter curr_flt_arr_iter;
                dbus_message_iter_recurse(&curr_flt_struct_iter, &curr_flt_arr_iter);
                if (dbus_message_iter_get_arg_type(&curr_flt_arr_iter) != DBUS_TYPE_STRUCT) {
                     // D-Bus response current_filter is not a struct
                     return SUCCESS;
                }
                DBusMessageIter curr_flt_extn_iter;
                dbus_message_iter_recurse(&curr_flt_arr_iter, &curr_flt_extn_iter);
                if (dbus_message_iter_get_arg_type(&curr_flt_extn_iter) != DBUS_TYPE_UINT32) {
                     // D-Bus response URI is not a string
                     return SUCCESS;
                }
                dbus_uint32_t type;
                dbus_message_iter_get_basic(&curr_flt_extn_iter, &type);
                if (type != 0) {
                     // Wrong filter type
                     return SUCCESS;
                }
                if (!dbus_message_iter_next(&curr_flt_extn_iter)) {
                     // D-Bus response current_filter struct ended prematurely
                     return SUCCESS;
                }
                if (dbus_message_iter_get_arg_type(&curr_flt_extn_iter) != DBUS_TYPE_STRING) {
                     // D-Bus response URI is not a string
                     return SUCCESS;
                }
                dbus_message_iter_get_basic(&curr_flt_extn_iter, &tmp_extn);
                return SUCCESS;
            }) == ERROR)
        return ERROR;

    if (!tmp_file) {
        setErrorText("D-Bus response has no URI field");
        return ERROR;
    }
    file = tmp_file;
    extn = tmp_extn;
    if (tmp_curr_filter) {
        Free((void*)selFilter->pattern);
        Free((void*)selFilter->name);
        selFilter->name = strdup(tmp_curr_filter);
        selFilter->pattern = strdup(extn);
    }
    return SUCCESS;
}
#endif

char* generateChars(char* out) {
    size_t count = 32;
    while (count > 0) {
        unsigned char buff[32];
        //ssize_t rnd = getrandom(buff, count, 0);
        ssize_t rnd = syscall(SYS_getrandom, buff, count, 0);
        if (rnd == -1) {
            if (errno == EINTR)
                continue;
            else
                break;
        }
        count -= rnd;
        // must be [A-Z][a-z][0-9]_
        for (size_t i = 0; i != static_cast<size_t>(rnd); ++i) {
            *out++ = 'A' + static_cast<char>(buff[i] & 15);
            *out++ = 'A' + static_cast<char>(buff[i] >> 4);
        }
    }
    return out;
}

char* createUniquePath(const char** handle_token) {
    const char RESPONSE_PATH[] = "/org/freedesktop/portal/desktop/request/";
    constexpr size_t RESPONSE_PATH_SIZE = sizeof(RESPONSE_PATH) - 1;
    const char* dbus_name = dbus_unique_name;
    if (*dbus_name == ':')
        ++dbus_name;
    const size_t sender_len = strlen(dbus_name);
    const size_t size = RESPONSE_PATH_SIZE + sender_len + 1 + 64;  // 1 for '/'
    char* path = (char*)malloc(size + 1);
    char* path_ptr = path;
    path_ptr = strcopy(RESPONSE_PATH, RESPONSE_PATH + RESPONSE_PATH_SIZE, path_ptr);
    path_ptr = replaceSymbol(dbus_name, dbus_name + sender_len, path_ptr, [](char chr) {
        return (chr != '.') ? chr : '_';
    });
    *path_ptr++ = '/';
    *handle_token = path_ptr;
    path_ptr = generateChars(path_ptr);
    *path_ptr = '\0';
    return path;
}

bool isHex(char ch) {
    return ('0' <= ch && ch <= '9') ||
           ('A' <= ch && ch <= 'F') ||
           ('a' <= ch && ch <= 'f');
}

bool tryUriDecodeLen(const char* fileUri, size_t &out, const char* &fileUriEnd) {
    size_t len = 0;
    while (*fileUri) {
        if (*fileUri != '%') {
            ++fileUri;
        } else {
            if (*(fileUri + 1) == '\0' || *(fileUri + 2) == '\0') {
                return false;
            }
            if (!isHex(*(fileUri + 1)) || !isHex(*(fileUri + 2))) {
                return false;
            }
            fileUri += 3;
        }
        ++len;
    }
    out = len;
    fileUriEnd = fileUri;
    return true;
}

char parseHex(char chr) {
    if ('0' <= chr && chr <= '9')
        return chr - '0';
    if ('A' <= chr && chr <= 'F')
        return chr - ('A' - 10);
    if ('a' <= chr && chr <= 'f')
        return chr - ('a' - 10);
#if defined(__GNUC__)
    __builtin_unreachable();
#endif
}

char* uriDecodeUnchecked(const char* fileUri, const char* fileUriEnd, char* outPath) {
    while (fileUri != fileUriEnd) {
        if (*fileUri != '%') {
            *outPath++ = *fileUri++;
        } else {
            ++fileUri;
            const char high_nibble = parseHex(*fileUri++);
            const char low_nibble = parseHex(*fileUri++);
            *outPath++ = (high_nibble << 4) | low_nibble;
        }
    }
    return outPath;
}

Result allocAndCopyFilePath(const char* fileUri, char* &outPath) {
    const char* prefix_begin = URI_PREFIX;
    const char* const prefix_end = URI_PREFIX + URI_PREFIX_SIZE;
    for (; prefix_begin != prefix_end; ++prefix_begin, ++fileUri) {
        if (*prefix_begin != *fileUri) {
            setErrorText("Portal returned not a file URI");
            return ERROR;
        }
    }
    size_t decoded_len;
    const char* file_uri_end;
    if (!tryUriDecodeLen(fileUri, decoded_len, file_uri_end)) {
        setErrorText("Portal returned a malformed URI");
        return ERROR;
    }
    char* const path_without_prefix = (char*)malloc(decoded_len + 1);
    char* const out_end = uriDecodeUnchecked(fileUri, file_uri_end, path_without_prefix);
    *out_end = '\0';
    outPath = path_without_prefix;
    return SUCCESS;
}

#ifdef ADD_EXTENSION
bool tryGetExtension(const char* extn,
                     const char* &trimmed_extn,
                     const char* &trimmed_extn_end) {
    if (!extn)
        return false;
    if (*extn != '*')
        return false;
    ++extn;
    if (*extn != '.')
        return false;
    trimmed_extn = extn;
    for (++extn; *extn != '\0'; ++extn)
        ;
    ++extn;
    trimmed_extn_end = extn;
    return true;
}

Result allocAndCopyFilePathWithExtn(const char* fileUri, const char* extn, char* &outPath) {
    const char* prefix_begin = URI_PREFIX;
    const char* const prefix_end = URI_PREFIX + URI_PREFIX_SIZE;
    for (; prefix_begin != prefix_end; ++prefix_begin, ++fileUri) {
        if (*prefix_begin != *fileUri) {
            setErrorText("D-Bus portal returned a not file URI");
            return ERROR;
        }
    }

    size_t decoded_len;
    const char* file_uri_end;
    if (!tryUriDecodeLen(fileUri, decoded_len, file_uri_end)) {
        setErrorText("D-Bus portal returned a malformed URI");
        return ERROR;
    }
    const char* file_it = file_uri_end;

    do {
        --file_it;
    } while (*file_it != '/' && *file_it != '.');
    const char* trimmed_extn;      // includes the '.'
    const char* trimmed_extn_end;  // includes the '\0'
    if (*file_it == '.' || !tryGetExtension(extn, trimmed_extn, trimmed_extn_end)) {
        // has file extension or no valid extension
        char* const path_without_prefix = (char*)malloc(decoded_len + 1);
        char* const out_end = uriDecodeUnchecked(fileUri, file_uri_end, path_without_prefix);
        *out_end = '\0';
        outPath = path_without_prefix;
    } else {
        // no file extension
        char* const path_without_prefix = (char*)malloc(decoded_len + (trimmed_extn_end - trimmed_extn));
        char* const out_mid = uriDecodeUnchecked(fileUri, file_uri_end, path_without_prefix);
        char* const out_end = strcopy(trimmed_extn, trimmed_extn_end, out_mid);
        *out_end = '\0';
        outPath = path_without_prefix;
    }
    return SUCCESS;
}
#endif

Result callXdgPortal(Window parent, Xdg::Mode mode, const char* title,
                     DBusMessage* &outMsg,
                     const FilterItem* filterList,
                     uint filterCount,
                     FilterItem* selFilter,
                     const char* defltPath,
                     const char* defltName,
                     bool multiple) {
    const char* handle_token;
    char* handle_path = createUniquePath(&handle_token);
    FreeLater __freeLater(handle_path);
    DBusError err;
    dbus_error_init(&err);

    DBusSignalHandler signal_hand;
    Result res = signal_hand.subscribe(handle_path);
    if (res != SUCCESS)
        return res;

    DBusMessage* methd = dbus_message_new_method_call("org.freedesktop.portal.Desktop",
                                                      "/org/freedesktop/portal/desktop",
                                                      "org.freedesktop.portal.FileChooser",
                                                      (mode == Xdg::Mode::SAVE)
                                                        ? "SaveFile" : "OpenFile");
    UnrefLater_DBusMessage __unrefLater(methd);
    DBusMessageIter iter;
    dbus_message_iter_init_append(methd, &iter);

    QString parent_window_qstr = "x11:" + QString::number((long)parent, 16);
    char* parent_window = parent_window_qstr.toUtf8().data();
    __dbusAppend(&iter, DBUS_TYPE_STRING, &parent_window);
    __dbusAppend(&iter, DBUS_TYPE_STRING, &title);

    DBusMessageIter arr_iter;
    __dbusOpen(&iter, DBUS_TYPE_ARRAY, "{sv}", &arr_iter);
    setHandleToken(arr_iter, handle_token);

    if (mode == Xdg::Mode::SAVE) {
        // Save file
        setFilters(arr_iter, filterList, filterCount, selFilter);
        setCurrentName(arr_iter, defltName);
        setCurrentFolder(arr_iter, defltPath);
        setCurrentFile(arr_iter, defltPath, defltName);
    } else
    if (mode == Xdg::Mode::OPEN) {
        // Open file(s)
        if (multiple)
            setOpenFileEntryType(arr_iter, EntryType::Multiple);

        setFilters(arr_iter, filterList, filterCount, selFilter);
    } else {
        // Open folder
        setOpenFileEntryType(arr_iter, EntryType::Directory);
    }
    __dbusClose(&iter, &arr_iter);

    DBusMessage* reply = dbus_connection_send_with_reply_and_block(
                            dbus_conn, methd, DBUS_TIMEOUT_INFINITE, &err);
    if (!reply) {
        dbus_error_free(&dbus_err);
        dbus_move_error(&err, &dbus_err);
        setErrorText(dbus_err.message);
        return ERROR;
    }
    UnrefLater_DBusMessage __replyUnrefLater(reply);
    {
        DBusMessageIter iter;
        if (!dbus_message_iter_init(reply, &iter)) {
            setErrorText("D-Bus reply is missing an argument");
            return ERROR;
        }
        if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_OBJECT_PATH) {
            setErrorText("D-Bus reply is not an object path");
            return ERROR;
        }
        const char* path;
        dbus_message_iter_get_basic(&iter, &path);
        if (strcmp(path, handle_path) != 0) {
            signal_hand.subscribe(path);
        }
    }

    do {
        while (true) {
            DBusMessage* msg = dbus_connection_pop_message(dbus_conn);
            if (!msg)
                break;
            if (dbus_message_is_signal(msg, "org.freedesktop.portal.Request", "Response")) {
                outMsg = msg;
                return SUCCESS;
            }
            dbus_message_unref(msg);
        }
    } while (dbus_connection_read_write(dbus_conn, -1));

    setErrorText("Portal did not give a reply");
    return ERROR;
}

const char* getErrorText(void) {
    return error_code;
}

void clearDBusError(void) {
    setErrorText(nullptr);
    dbus_error_free(&dbus_err);
}

Result initDBus(void) {
    dbus_error_init(&dbus_err);
    dbus_conn = dbus_bus_get(DBUS_BUS_SESSION, &dbus_err);
    if (!dbus_conn) {
        setErrorText(dbus_err.message);
        return ERROR;
    }
    dbus_unique_name = dbus_bus_get_unique_name(dbus_conn);
    if (!dbus_unique_name) {
        setErrorText("Cannot get name of connection");
        return ERROR;
    }
    return SUCCESS;
}

void quitDBus(void) {
    dbus_connection_unref(dbus_conn);
}

void freePath(char* filePath) {
    assert(filePath);
    Free(filePath);
}

Result openDialog(Window parent, Xdg::Mode mode, const char* title,
                  char** outPaths,
                  const FilterItem* filterList,
                  uint filterCount,
                  FilterItem* selFilter,
                  const char* defltPath,
                  const char* defltName,
                  bool multiple) {
    DBusMessage* msg;
    {
        const Result res = callXdgPortal(parent, mode, title,
                                         msg,
                                         filterList,
                                         filterCount,
                                         selFilter,
                                         defltPath,
                                         defltName,
                                         multiple);
        if (res != SUCCESS)
            return res;
    }

    if (mode == Xdg::Mode::OPEN) {
        if (!multiple) {
            // Open file
            UnrefLater_DBusMessage __msgUnrefLater(msg);
            const char* uri;
            {
                const Result res = readResponseUrisSingle(msg, uri);
                if (res != SUCCESS)
                    return res;
                readResponseCurrentFilter(msg, selFilter);
            }
            return allocAndCopyFilePath(uri, *outPaths);
        } else {
            // Open files
            DBusMessageIter uri_iter;
            const Result res = readResponseUris(msg, uri_iter);
            if (res != SUCCESS) {
                dbus_message_unref(msg);
                return res;
            }
            *outPaths = (char*)msg;
            return SUCCESS;
        }

    } else
    if (mode == Xdg::Mode::SAVE) {
        // Save file
        UnrefLater_DBusMessage __msgUnrefLater(msg);
#ifdef ADD_EXTENSION
        const char* uri = NULL;
        const char* extn = NULL;
        {
            const Result res = readResponseUrisSingleAndCurrentExtension(
                                    msg, uri, extn, selFilter);
            if (res != SUCCESS) {
                return res;
            }
        }
        return allocAndCopyFilePathWithExtn(uri, extn, *outPaths);
#else
        const char* uri;
        {
            const Result res = readResponseUrisSingle(msg, uri);
            if (res != SUCCESS)
                return res;
            readResponseCurrentFilter(msg, selFilter);
        }
        return allocAndCopyFilePath(uri, *outPaths);
#endif
    } else {
        // Open folder
        UnrefLater_DBusMessage __msgUnrefLater(msg);
        const char* uri;
        {
            const Result res = readResponseUrisSingle(msg, uri);
            if (res != SUCCESS)
                return res;
        }
        return allocAndCopyFilePath(uri, *outPaths);
    }
}

Result pathSetGetCount(const void* pathSet,
                       uint* count) {
    assert(pathSet);
    DBusMessage* msg = const_cast<DBusMessage*>(static_cast<const DBusMessage*>(pathSet));
    *count = readResponseUrisUncheckedSize(msg);
    return SUCCESS;
}

Result pathSetGetPath(const void* pathSet,
                            uint index,
                            char** outPath) {
    assert(pathSet);
    DBusMessage* msg = const_cast<DBusMessage*>(static_cast<const DBusMessage*>(pathSet));
    DBusMessageIter uri_iter;
    readResponseUrisUnchecked(msg, uri_iter);
    while (index > 0) {
        --index;
        if (!dbus_message_iter_next(&uri_iter)) {
            setErrorText("Index out of bounds");
            return ERROR;
        }
    }
    if (dbus_message_iter_get_arg_type(&uri_iter) != DBUS_TYPE_STRING) {
        setErrorText("D-Bus response URI is not a string");
        return ERROR;
    }
    const char* uri;
    dbus_message_iter_get_basic(&uri_iter, &uri);
    return allocAndCopyFilePath(uri, *outPath);
}

void pathSetFreePath(const char* filePath) {
    assert(filePath);
    freePath(const_cast<char*>(filePath));
}

void pathSetFree(const void* pathSet) {
    assert(pathSet);
    DBusMessage* msg = const_cast<DBusMessage*>(static_cast<const DBusMessage*>(pathSet));
    dbus_message_unref(msg);
}

char* strcopy(const char* start, const char* end, char* out) {
    for (; start != end; ++start) {
        *out++ = *start;
    }
    return out;
}

void setErrorText(const char* msg) {
    error_code = msg;
}

void Free(void* p) {
    if (p != NULL) {
        free(p);
        p = NULL;
    }
}

QStringList Xdg::openXdgPortal(QWidget *parent,
                               Mode mode,
                               const QString &title,
                               const QString &file_name,
                               const QString &path,
                               const QString &filter,
                               QString *sel_filter,
                               bool sel_multiple)
{
    initDBus();
    Window parentWid = (parent) ? (Window)parent->winId() : 0L;
    QStringList files;

    const int pos = file_name.lastIndexOf('/');
    const QString _file_name = (pos != -1) ?
                file_name.mid(pos + 1) : file_name;
    const QString _path = (path.isEmpty() && pos != -1) ?
                file_name.mid(0, pos) : path;

    QStringList filterList = filter.split(";;");
    int filterSize = filterList.size();
    FilterItem filterItem[filterSize];
    int index = 0;
    foreach (const QString &flt, filterList) {
        filterItem[index].name = strdup(flt.toUtf8().data());
        auto parse = flt.split('(');        
        if (parse.size() == 1) {
            filterItem[index].pattern = strdup("");
        } else
        if (parse.size() == 2) {
            const QString pattern = parse[1].replace(")", "");
            filterItem[index].pattern = strdup(pattern.toUtf8().data());
        }
        index++;
    }

    FilterItem selFilterItem;
    selFilterItem.name = strdup(sel_filter->toUtf8().data());
    selFilterItem.pattern = NULL;
    auto parse = sel_filter->split('(');
    if (parse.size() == 1) {
        selFilterItem.pattern = strdup("");
    } else
    if (parse.size() == 2) {
        const QString pattern = parse[1].replace(")", "");
        selFilterItem.pattern = strdup(pattern.toUtf8().data());
    }

    char* outPaths;
    Result result;
    result = openDialog(parentWid, mode, title.toUtf8().data(),
                        &outPaths,
                        filterItem,
                        filterSize,
                        &selFilterItem,
                        _path.toLocal8Bit().data(),
                        _file_name.toLocal8Bit().data(),
                        sel_multiple);

    if (mode == Mode::OPEN && sel_multiple) {
        if (result == Result::SUCCESS) {
            uint numPaths;
            pathSetGetCount(outPaths, &numPaths);
            for (uint i = 0; i < numPaths; ++i) {
                char* path = nullptr;
                pathSetGetPath(outPaths, i, &path);
                files.append(QString::fromUtf8(path));
                pathSetFreePath(path);
            }
            pathSetFree(outPaths);
        }
    } else {
        if (result == Result::SUCCESS) {
            files.append(QString::fromUtf8(outPaths));
            freePath(outPaths);
        }
    }

    if (result == Result::ERROR)
        printf("Error while open dialog: %s\n", getErrorText());

    quitDBus();

    Free((void*)selFilterItem.pattern);
    if (selFilterItem.name != NULL) {
        *sel_filter = QString::fromUtf8(selFilterItem.name);
        Free((void*)selFilterItem.name);
    }

    for (int i = 0; i < filterSize; i++) {
        Free((void*)filterItem[i].pattern);
        Free((void*)filterItem[i].name);
    }

    return files;
}


/** Print Dialog **/

typedef QPrinter::Unit QUnit;
typedef QHash<const char*, QVariant> VariantHash;

void setData(DBusMessageIter &msg_iter, const VariantHash &hash) {
    auto keys = hash.keys();
    foreach (const char* key, keys) {
        DBusMessageIter dict_iter;
        DBusMessageIter variant_iter;
        __dbusOpen(&msg_iter, DBUS_TYPE_DICT_ENTRY, nullptr, &dict_iter);
        __dbusAppend(&dict_iter, DBUS_TYPE_STRING, &key);

        if (strcmp(hash[key].typeName(), "QString") == 0) {
            __dbusOpen(&dict_iter, DBUS_TYPE_VARIANT, "s", &variant_iter);
            char* val = hash[key].toString().toUtf8().data();
            __dbusAppend(&variant_iter, DBUS_TYPE_STRING, &val);
            __dbusClose(&dict_iter, &variant_iter);
        } else
        if (strcmp(hash[key].typeName(), "double") == 0) {
            __dbusOpen(&dict_iter, DBUS_TYPE_VARIANT, "d", &variant_iter);
            double val = hash[key].toDouble();
            __dbusAppend(&variant_iter, DBUS_TYPE_DOUBLE, &val);
            __dbusClose(&dict_iter, &variant_iter);
        } else {
            g_print("Other type: %s\n", hash[key].typeName());
        }
        __dbusClose(&msg_iter, &dict_iter);
    }
}

Result readData(DBusMessage* msg, const char *response, VariantHash &hash) {
    DBusMessageIter iter;
    const Result res = readResponseResults(msg, iter);
    if (res != SUCCESS)
        return res;

    if (readDict(iter, response,
                 [&hash](DBusMessageIter& settings_iter) {
                    if (dbus_message_iter_get_arg_type(&settings_iter) == DBUS_TYPE_ARRAY) {
                         DBusMessageIter dict_iter;
                         dbus_message_iter_recurse(&settings_iter, &dict_iter);
                         while (dbus_message_iter_get_arg_type(&dict_iter) == DBUS_TYPE_DICT_ENTRY) {
                             DBusMessageIter key_iter;
                             dbus_message_iter_recurse(&dict_iter, &key_iter);
                             if (dbus_message_iter_get_arg_type(&key_iter) == DBUS_TYPE_STRING) {
                                  const char *key = NULL;
                                  dbus_message_iter_get_basic(&key_iter, &key);
                                  //g_print("Key: %s\n", key);
                                  if (dbus_message_iter_next(&key_iter)) {
                                     if (dbus_message_iter_get_arg_type(&key_iter) == DBUS_TYPE_VARIANT) {
                                         DBusMessageIter var_iter;
                                         dbus_message_iter_recurse(&key_iter, &var_iter);
                                         if (dbus_message_iter_get_arg_type(&var_iter) == DBUS_TYPE_STRING) {
                                             const char *str_val = NULL;
                                             dbus_message_iter_get_basic(&var_iter, &str_val);
                                             //g_print("Val: %s\n", str_val);
                                             if (key) {
                                                if (hash.contains(key))
                                                    hash[key] = QVariant(QString::fromUtf8(str_val));
                                             }
                                             //Free((void*)str_val);
                                         } else
                                         if (dbus_message_iter_get_arg_type(&var_iter) == DBUS_TYPE_DOUBLE) {
                                             double dbl_val;
                                             dbus_message_iter_get_basic(&var_iter, &dbl_val);
                                             //g_print("Val: %f\n", dbl_val);
                                             if (key) {
                                                if (hash.contains(key))
                                                    hash[key] = QVariant(dbl_val);
                                             }
                                         } else {
                                             g_print("Other type: %d\n", dbus_message_iter_get_arg_type(&var_iter));
                                         }
                                     }
                                  }
                                  //Free((void*)key);
                             }
                             if (!dbus_message_iter_next(&dict_iter))
                                 break;
                         }
                         return SUCCESS;
                    }
                    return ERROR;
                 }) == ERROR)
        return ERROR;

    return SUCCESS;
}

Result callXdgPrintDialog(Window parent,
                          const char* title,
                          const VariantHash &print_settings,
                          const VariantHash &page_setup,
                          DBusMessage* &outMsg) {

    const char* handle_token;
    char* handle_path = createUniquePath(&handle_token);
    FreeLater __freeLater(handle_path);
    DBusError err;
    dbus_error_init(&err);

    DBusSignalHandler signal_hand;
    Result res = signal_hand.subscribe(handle_path);
    if (res != SUCCESS)
        return res;

    DBusMessage* methd = dbus_message_new_method_call("org.freedesktop.portal.Desktop",
                                                      "/org/freedesktop/portal/desktop",
                                                      "org.freedesktop.portal.Print",
                                                      "PreparePrint");
    UnrefLater_DBusMessage __unrefLater(methd);
    DBusMessageIter iter;
    dbus_message_iter_init_append(methd, &iter);

    QString parent_window_qstr = "x11:" + QString::number((long)parent, 16);
    char* parent_window = parent_window_qstr.toUtf8().data();
    __dbusAppend(&iter, DBUS_TYPE_STRING, &parent_window);
    __dbusAppend(&iter, DBUS_TYPE_STRING, &title);

    DBusMessageIter arr_iter;
    __dbusOpen(&iter, DBUS_TYPE_ARRAY, "{sv}", &arr_iter);
    setData(arr_iter, print_settings);
    __dbusClose(&iter, &arr_iter);

    __dbusOpen(&iter, DBUS_TYPE_ARRAY, "{sv}", &arr_iter);
    setData(arr_iter, page_setup);
    __dbusClose(&iter, &arr_iter);

    __dbusOpen(&iter, DBUS_TYPE_ARRAY, "{sv}", &arr_iter);
    setHandleToken(arr_iter, handle_token);
    __dbusClose(&iter, &arr_iter);

    DBusMessage* reply = dbus_connection_send_with_reply_and_block(
                            dbus_conn, methd, DBUS_TIMEOUT_INFINITE, &err);
    if (!reply) {
        dbus_error_free(&dbus_err);
        dbus_move_error(&err, &dbus_err);
        setErrorText(dbus_err.message);
        return ERROR;
    }
    UnrefLater_DBusMessage __replyUnrefLater(reply);
    {
        DBusMessageIter iter;
        if (!dbus_message_iter_init(reply, &iter)) {
            setErrorText("D-Bus reply is missing an argument");
            return ERROR;
        }
        if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_OBJECT_PATH) {
            setErrorText("D-Bus reply is not an object path");
            return ERROR;
        }
        const char* path;
        dbus_message_iter_get_basic(&iter, &path);
        if (strcmp(path, handle_path) != 0) {
            signal_hand.subscribe(path);
        }
    }

    do {
        while (true) {
            DBusMessage* msg = dbus_connection_pop_message(dbus_conn);
            if (!msg)
                break;
            if (dbus_message_is_signal(msg, "org.freedesktop.portal.Request", "Response")) {
                outMsg = msg;
                return SUCCESS;
            }
            dbus_message_unref(msg);
        }
    } while (dbus_connection_read_write(dbus_conn, -1));

    setErrorText("Portal did not give a reply");
    return ERROR;
}

Result openPrintDialog(Window parent,
                       const char* title,
                       VariantHash &print_settings,
                       VariantHash &page_setup) {
    DBusMessage* msg;
    {
        const Result res = callXdgPrintDialog(parent,
                                              title,
                                              print_settings,
                                              page_setup,
                                              msg);
        if (res != SUCCESS)
            return res;
    }

    UnrefLater_DBusMessage __msgUnrefLater(msg);

    Result res;
    res = readData(msg, "settings", print_settings);
    if (res != SUCCESS)
        return res;

    res = readData(msg, "page-setup", page_setup);
    if (res != SUCCESS)
        return res;

    return SUCCESS;
}


XdgPrintDialog::XdgPrintDialog(QPrinter *printer, QWidget *parent) :
    m_printer(printer),
    m_parent(parent),
    m_title(QString()),
    m_options(PrintOptions()),
    m_print_range(PrintRange::AllPages)
{
    m_print_range = (PrintRange)printer->printRange();
    if (m_printer->collateCopies())
        m_options |= PrintOption::PrintCollateCopies;

}

XdgPrintDialog::~XdgPrintDialog()
{

}

void XdgPrintDialog::setWindowTitle(const QString &title)
{
    m_title = title;
}

void XdgPrintDialog::setEnabledOptions(PrintOptions enbl_opts)
{
    m_options = enbl_opts;
}

void XdgPrintDialog::setOptions(PrintOptions opts)
{
    m_options = opts;
}

void XdgPrintDialog::setPrintRange(PrintRange print_range)
{
    m_print_range = print_range;
}

QDialog::DialogCode XdgPrintDialog::exec()
{
    QDialog::DialogCode exit_code = QDialog::DialogCode::Rejected;
    Window parent_xid = (m_parent) ? (Window)m_parent->winId() : 0L;

    //auto qt_printer_name = m_printer->printerName();
    auto qt_resolution = m_printer->resolution();
    auto qt_orient = m_printer->orientation();
    auto qt_duplex = m_printer->duplex();
    auto qt_color_mode = m_printer->colorMode();
    auto qt_copy_count = m_printer->copyCount();
    auto qt_page_order = m_printer->pageOrder();
    auto qt_output_filename = m_printer->outputFileName();
    //auto qt_doc_name = m_printer->docName();
    //auto qt_full_page = m_printer->fullPage();
    //auto qt_color_count = m_printer->colorCount();
    //auto qt_supported_res = m_printer->supportedResolutions();
    //auto qt_supports_multi_copies = m_printer->supportsMultipleCopies();
    //auto qt_selection_option = m_printer->printerSelectionOption();
    //auto qt_output_format = m_printer->outputFormat();
    //auto qt_paper_source = m_printer->paperSource();

    // Qt-PrintOptions:
    // None = 0
    // PrintToFile = 1          - not applied
    // PrintSelection = 2       - not applied
    // PrintPageRange = 4       - not applied
    // PrintShowPageSize = 8    - not applied
    // PrintCollateCopies = 16  - not applied
    // DontUseSheet = 32        - not applied
    // PrintCurrentPage = 64    - not applied

    // Input settings
    const QString quality_arr[] = {
        "low",
        "normal",
        "high",
        "draft"
    };

    // Qt-PrintRange:
    // AllPages = 0
    // Selection = 1
    // PageRange = 2
    // CurrentPage = 3
    const QString print_pages_arr[] = {
        "all",
        "selection",
        "ranges",
        "current"
    };
    const int print_range = (int)m_print_range;
    const QString print_pages = (print_range >= 0 && print_range <= 4) ?
                print_pages_arr[m_print_range] : print_pages_arr[0];

    const QString page_ranges = QString::number(m_printer->fromPage()) + "-" +
                                QString::number(m_printer->toPage());

    const QString page_set_arr[] = {
        "all",
        "even",
        "odd"
    };

    // Qt-Duplex:
    // DuplexNone = 0
    // DuplexAuto = 1       - not applied
    // DuplexLongSide = 2
    // DuplexShortSide = 3
    const QString duplex_arr[] = {
        "simplex",
        "horizontal",
        "vertical"
    };
    const QString duplex = (qt_duplex == QPrinter::DuplexLongSide) ?  duplex_arr[1] :
                           (qt_duplex == QPrinter::DuplexShortSide) ? duplex_arr[2] :
                                                                      duplex_arr[0];
    const QString collate("yes");
    const QString use_color = qt_color_mode == QPrinter::Color ? "yes" : "no";
    const QString reverse = qt_page_order == QPrinter::LastPageFirst ? "yes" : "no";

    VariantHash print_settings = {
        //{"orientation",         QVariant("")}, // portrait landscape reverse-portrait reverse-landscape
        //{"paper-format",        QVariant("")}, // according to PWG 5101.1-2002
        //{"paper-width",         QVariant("")}, // mm
        //{"paper-height",        QVariant("")}, // mm
        {"n-copies",            QVariant(QString::number(qt_copy_count))},
        //{"default-source",      QVariant("")},
        {"quality",             QVariant(quality_arr[2])}, // normal high low draft
        {"resolution",          QVariant(QString::number(qt_resolution))}, // The resolution, sets both resolution-x and resolution-y
        {"use-color",           QVariant(use_color)}, // true false
        {"duplex",              QVariant(duplex)}, // simplex horizontal vertical
        {"collate",             QVariant(collate)}, // true false
        {"reverse",             QVariant(reverse)}, // true false
        //{"media-type",          QVariant("")}, // according to PWG 5101.1-2002
        //{"dither",              QVariant("")}, // The dithering to use: fine none coarse lineart grayscale error-diffusion
        //{"scale",               QVariant("")}, // The scale in percent
        {"print-pages",         QVariant(print_pages)}, // all selection current ranges
        {"page-ranges",         QVariant(page_ranges)}, // Note that page ranges are 0-based, even if the are displayed as 1-based when presented to the user
        {"page-set",            QVariant(page_set_arr[0])}, // all even odd
        //{"finishings",          QVariant("")},
        //{"number-up",           QVariant("")}, // The number of pages per sheet
        //{"number-up-layout",    QVariant("")}, // lrtb lrbt rltb rlbt tblr tbrl btlr btrl
        //{"output-bin",          QVariant("")},
        //{"resolution-x",        QVariant("")}, // dpi
        //{"resolution-y",        QVariant("")}, // dpi
        //{"printer-lpi",         QVariant("")}, // The resolution in lines per inch
        //{"output-basename",     QVariant("")}, // Basename to use for print-to-file
        //{"output-file-format",  QVariant("")}, // Format to use for print-to-file: PDF PS SVG
        {"output-uri",          QVariant(qt_output_filename)}  // The uri used for print-to-file
    };

   // Input page setup
    QUnit qt_unit(QUnit::Millimeter);
    double left_in, top_in, right_in, bottom_in;
    m_printer->getPageMargins(&left_in, &top_in, &right_in, &bottom_in, qt_unit);

    const int width_in = (qt_orient == QPrinter::Portrait) ?
                m_printer->widthMM() : m_printer->heightMM();
    const int height_in = (qt_orient == QPrinter::Portrait) ?
                m_printer->heightMM() : m_printer->widthMM();

    // Qt-Orient:
    // Portrait = 0
    // Landscape = 1
    const QString orientation_arr[] = {
        "portrait",
        "landscape",
        "reverse_portrait",
        "reverse_landscape"
    };
    const int print_ornt = (int)qt_orient;
    const QString orientation = (print_ornt >= 0 && print_ornt <= 4) ?
                orientation_arr[print_ornt] : orientation_arr[0];

    VariantHash page_setup = {
        {"PPDName",         QVariant(m_printer->paperName())}, // The PPD name
        //{"Name",            QVariant("")}, // The name of the page setup
        {"DisplayName",     QVariant(m_printer->paperName())}, // User-visible name for the page setup
        {"Width",           QVariant(double(width_in))}, // d mm
        {"Height",          QVariant(double(height_in))}, // d mm
        {"MarginTop",       QVariant(top_in)}, // d mm
        {"MarginBottom",    QVariant(bottom_in)}, // d mm
        {"MarginLeft",      QVariant(left_in)}, // d mm
        {"MarginRight",     QVariant(right_in)}, // d mm
        {"Orientation",     QVariant(orientation)}  // portrait landscape reverse-portrait reverse-landscape
    };

    // Init dialog
    initDBus();
    Result result;
    result = openPrintDialog(parent_xid,
                             m_title.toUtf8().data(),
                             print_settings,
                             page_setup);

    if (result == Result::SUCCESS) {
        enum _ColorMode {_GrayScale, _Color}; // GrayScale is defined already
        const QString use_color = print_settings["use-color"].toString();
        m_printer->setColorMode(use_color == "yes" ?
                      QPrinter::ColorMode(_Color) : QPrinter::ColorMode(_GrayScale));

        const QString print_pages = print_settings["print-pages"].toString();
        PrintRange range_arr[4] = {
            PrintRange::AllPages,
            PrintRange::Selection,
            PrintRange::PageRange,
            PrintRange::CurrentPage
        };
        m_print_range = (print_pages == print_pages_arr[0]) ? range_arr[0] :
                        (print_pages == print_pages_arr[1]) ? range_arr[1] :
                        (print_pages == print_pages_arr[2]) ? range_arr[2] :
                        (print_pages == print_pages_arr[3]) ? range_arr[3] :
                                                              range_arr[0];

        const QString page_ranges = print_settings["page-ranges"].toString();
        foreach (const QString& range, page_ranges.split(',')) {
            auto interval = range.split('-');
            int start = 1;
            int end = 1;
            if (interval.size() == 1) {
                start = interval[0].toInt() + 1;
                end = interval[0].toInt() + 1;
            } else
            if (interval.size() == 2) {
                start = interval[0].toInt() + 1;
                end = interval[1].toInt() + 1;
            }
            m_printer->setFromTo(start, end);
            break; // only single range supported for QPrinter
        }

        const QString collate = print_settings["collate"].toString();
        m_printer->setCollateCopies(collate == "yes" ? true : false);

        const QString duplex = print_settings["duplex"].toString();
        QPrinter::DuplexMode qtduplex_arr[4] = {
            QPrinter::DuplexNone,
            QPrinter::DuplexAuto,
            QPrinter::DuplexLongSide,
            QPrinter::DuplexShortSide
        };
        m_printer->setDuplex(duplex == "horizontal" ? qtduplex_arr[2] :
                             duplex == "vertical" ?   qtduplex_arr[3] :
                                                      qtduplex_arr[0]);

        const QString reverse = print_settings["reverse"].toString();
        m_printer->setPageOrder(reverse == "yes" ?
                    QPrinter::LastPageFirst : QPrinter::FirstPageFirst);

        const QString n_copies = print_settings["n-copies"].toString();
        m_printer->setNumCopies(n_copies.toInt() > 0 ? n_copies.toInt() : 1);

        QString output_uri = print_settings["output-uri"].toString();
        m_printer->setOutputFileName(output_uri.replace("file://", ""));

        QUnit qt_unit(QUnit::Millimeter);
        gdouble left = page_setup["MarginLeft"].toDouble();
        gdouble top = page_setup["MarginTop"].toDouble();
        gdouble right = page_setup["MarginRight"].toDouble();
        gdouble bottom = page_setup["MarginBottom"].toDouble();
        m_printer->setPageMargins(left, top, right, bottom, qt_unit);

        const QString paper_name = page_setup["PPDName"].toString();
        gdouble width = page_setup["Width"].toDouble();
        gdouble height = page_setup["Height"].toDouble();
        m_printer->setPaperName(paper_name);
        m_printer->setPaperSize(QSizeF(width, height), qt_unit);

        const QString orient = page_setup["Orientation"].toString();
        QPrinter::Orientation orient_arr[2] = {
            QPrinter::Portrait,
            QPrinter::Landscape
        };
        m_printer->setOrientation(orient == orientation_arr[0] ? orient_arr[0] :
                                  orient == orientation_arr[1] ? orient_arr[1] :
                                  orient == orientation_arr[2] ? orient_arr[0] :
                                  orient == orientation_arr[3] ? orient_arr[1] :
                                                                 orient_arr[0]);
        exit_code = QDialog::DialogCode::Accepted;
    } else
    if (result == Result::ERROR)
        g_print("Error while open dialog: %s\n", getErrorText());

    quitDBus();
    return exit_code;
}

PrintRange XdgPrintDialog::printRange()
{
    return m_print_range;
}

PrintOptions XdgPrintDialog::options()
{
    return m_options;
}

int XdgPrintDialog::fromPage()
{
    return m_printer->fromPage();
}

int XdgPrintDialog::toPage()
{
    return m_printer->toPage();
}
