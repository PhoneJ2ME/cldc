/*
 *
 *
 * Copyright  1990-2008 Sun Microsystems, Inc. All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 only, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details (a copy is
 * included at /legal/license.txt).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this work; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 or visit www.sun.com if you need additional
 * information or have any questions.
 */

/**
 * @file
 *
 * Implementation of javacall lifecycle notification functions.
 */

#include <string.h>
#include <jvm.h>
#include <midpServices.h>
#include <midpMalloc.h>
#include <midp_logging.h>
#include <localeMethod.h>
#include <midp_jc_event_defs.h>
#include <midp_properties_port.h>
#include "javautil_string.h"
#include <javacall_datagram.h>
#include <javacall_events.h>
#include <javacall_lifecycle.h>
#include <javacall_time.h>
#include <javautil_unicode.h>
#include <javacall_properties.h>
#include "runMidlet.h"

#include "midp_thread.h"
#include "midp_slavemode_port.h"

#define DEFAULT_MEMORY_CHUNK_SIZE (5*1024*1024 + 200*1024)
#define LOCALE "microedition.locale"

static char urlAddress[BINARY_BUFFER_MAX_LEN];

/**
 * Helper function to read memory parameters and initialize memory
 *
 * @return JAVACALL_OK if memory successfully initialized
 */
static javacall_result
initialize_memory_slavemode(void) {
    int main_memory_chunk_size;

    /* Get java heap memory size */
    main_memory_chunk_size = getInternalPropertyInt("MAIN_MEMORY_CHUNK_SIZE");
    if (0 == main_memory_chunk_size){
        REPORT_ERROR(LC_AMS, "javanotify_start(): Missing MAIN_MEMORY_CHUNK_SIZE property."
                      "System will exit!\n");
        main_memory_chunk_size = DEFAULT_MEMORY_CHUNK_SIZE;
        return JAVACALL_FAIL;
    }

    /* Initialize midp memory pool */
    if (midpInitializeMemory(main_memory_chunk_size) != 0) {
        REPORT_ERROR(LC_AMS, "javanotify_start(): Cannot initialize MIDP memory\n");
        return JAVACALL_OUT_OF_MEMORY;
    }

    return JAVACALL_OK;
}

/**
 * The platform should invoke this function in platform context to start
 * Java.
 */
void javanotify_start(void) {
    int argc = 0;
    char *argv[MIDP_RUNMIDLET_MAXIMUM_ARGS];
    javacall_result res;

    REPORT_INFO(LC_CORE, "javanotify_start() >>\n");

    JVM_Initialize();

    if (initialize_memory_slavemode() != JAVACALL_OK) {
        return;
    }

    argv[argc++] = "runMidlet";
    argv[argc++] = "-1";
    argv[argc++] =
#if ENABLE_MULTIPLE_ISOLATES
    "com.sun.midp.appmanager.MVMManager";
#else
    "com.sun.midp.appmanager.Manager";
#endif /* if ENABLE_MULTIPLE_ISOLATES */

    javacall_lifecycle_state_changed(JAVACALL_LIFECYCLE_MIDLET_STARTED, JAVACALL_OK,NULL);
    res = runMidlet(argc, argv);
}

/**
 * The platform should invoke this function in platform context to start
 * a specified MIDlet suite.
 *
 * @param suiteId the ID of the suite to start
 */
void javanotify_start_suite(char* suiteId) {
    int length;
    int argc = 0;
    char *argv[MIDP_RUNMIDLET_MAXIMUM_ARGS];
    javacall_result res;

    REPORT_INFO(LC_CORE, "javanotify_start_suite() >>\n");

    JVM_Initialize();

    if (initialize_memory_slavemode() != JAVACALL_OK) {
        return;
    }

    length = strlen(suiteId);
    if (length >= BINARY_BUFFER_MAX_LEN) {
        REPORT_ERROR(LC_AMS, "javanotify_start_suite(): Incorrect suiteId parameter\n");
        return;
    }
    memset(urlAddress, 0, BINARY_BUFFER_MAX_LEN);
    memcpy(urlAddress, suiteId, length);

    argv[argc++] = "runMidlet";
    argv[argc++] = "-1";
    argv[argc++] = "com.sun.midp.appmanager.MIDletSuiteLauncher";
    argv[argc++] = urlAddress;

    javacall_lifecycle_state_changed(JAVACALL_LIFECYCLE_MIDLET_STARTED, JAVACALL_OK, NULL);

    res = runMidlet(argc, argv);
}

/**
 * The platform should invoke this function in platform context to start
 * Java in local context (not OTA).
 */
void javanotify_start_local(char* classname, char* descriptor,
                            char* classpath, javacall_bool debug) {
    int argc = 0;
    char *argv[MIDP_RUNMIDLET_MAXIMUM_ARGS];
    javacall_result res;

    REPORT_INFO2(LC_CORE,"javanotify_start_local() >> classname=%s, descriptor=%d \n",
                 classname, descriptor);

    JVM_Initialize();
    if (initialize_memory_slavemode() != JAVACALL_OK) {
        return;
    }

    argv[argc++] = "runMidlet";
    argv[argc++] = "-1";

    if (classname == NULL) {
        argv[argc++] = "internal";
    } else {
        argv[argc++] = classname;
    }

    if (descriptor != NULL) {
        argv[argc++] = descriptor;
    } else {
        argv[argc++] = classpath;
    }

    if (classpath != NULL) {
        argv[argc++] = "-classpathext";
        argv[argc++] = classpath;
    }

    javacall_lifecycle_state_changed(JAVACALL_LIFECYCLE_MIDLET_STARTED, JAVACALL_OK, NULL);

    res = runMidlet(argc, argv);
}

/**
 * The platform should invoke this function in platform context to start
 * the Java VM and run TCK.
 *
 * @param url the http location of the TCK server
 *            the url should be of the form: "http://host:port"
 * @param domain the TCK execution domain
 */
void javanotify_start_tck(char *tckUrl, javacall_lifecycle_tck_domain domain_type) {
    int length;
    int argc = 0;
    char *argv[MIDP_RUNMIDLET_MAXIMUM_ARGS];
    javacall_result res;

    REPORT_INFO2(LC_CORE,"javanotify_start_tck() >> tckUrl=%s, domain_type=%d \n",tckUrl,domain_type);

    midp_thread_set_timeslice_proc(midp_slavemode_schedule_vm_timeslice);
    JVM_Initialize();

    if (initialize_memory_slavemode() != JAVACALL_OK) {
        return;
    }

    argv[argc++] = "runMidlet";
    argv[argc++] = "-1";
    argv[argc++] = "com.sun.midp.installer.AutoTester";

    length = strlen(tckUrl);
    if (length >= BINARY_BUFFER_MAX_LEN) {
        REPORT_ERROR(LC_AMS, "javanotify_start_tck(): tckUrl is too long");
        return;
    }

    memset(urlAddress, 0, BINARY_BUFFER_MAX_LEN);
    memcpy(urlAddress, tckUrl, length);
    if (strcmp(urlAddress, "none") != 0) {
        argv[argc++] = urlAddress;
    }

    switch (domain_type) {
    case JAVACALL_LIFECYCLE_TCK_DOMAIN_UNTRUSTED: 
        argv[argc] = "untrusted";
        break;
    case JAVACALL_LIFECYCLE_TCK_DOMAIN_TRUSTED:
        argv[argc] = "trusted";
        break;
    case JAVACALL_LIFECYCLE_TCK_DOMAIN_UNTRUSTED_MIN:
        argv[argc] = "minimum";
        break;
    case JAVACALL_LIFECYCLE_TCK_DOMAIN_UNTRUSTED_MAX:
        argv[argc] = "maximum";
        break;
    case JAVACALL_LIFECYCLE_TCK_DOMAIN_MANUFACTURER:
        argv[argc] = "manufacturer";
        break;
    case JAVACALL_LIFECYCLE_TCK_DOMAIN_OPERATOR:
        argv[argc] = "operator";
        break;
    case JAVACALL_LIFECYCLE_TCK_DOMAIN_IDENTIFIED:
        argv[argc] = "identified_third_party";
        break;
    case JAVACALL_LIFECYCLE_TCK_DOMAIN_UNIDENTIFIED:
        argv[argc] = "unidentified_third_party";
        break;
    default:
        REPORT_ERROR(LC_CORE, "javanotify_start_tck() [slave mode] Can not recognize TCK domain\n");
        REPORT_ERROR1(LC_CORE, "TCK domain type is %d. System will now exit\n", domain_type);
        return;
    }
    argc++;

    javacall_lifecycle_state_changed(JAVACALL_LIFECYCLE_MIDLET_STARTED, JAVACALL_OK, NULL);

    res = runMidlet(argc, argv);
}

/**
 * The platform should invoke this function in platform context to start
 * the Java VM and run i3test framework.
 *
 * @param arg1 optional argument 1
 * @param arg2 optional argument 2
 *
 * @note allowed argument description can be obtained by '-help' value as arg1.
 */
void javanotify_start_i3test(char* arg1, char* arg2) {
    int argc = 0;
    char *argv[MIDP_RUNMIDLET_MAXIMUM_ARGS];
    javacall_result res;

    REPORT_INFO2(LC_CORE,"javanotify_start_i3test() >> %s %s\n",arg1,arg2);

    JVM_Initialize();

    if (initialize_memory_slavemode() != JAVACALL_OK) {
        return;
    }

    argv[argc++] = "runMidlet";
    argv[argc++] = "-1";
    argv[argc++] = "com.sun.midp.i3test.Framework";
    if (NULL != arg1) {
        argv[argc++] = arg1;
        if (NULL != arg2)
            argv[argc++] = arg2;
    }

    javacall_lifecycle_state_changed(JAVACALL_LIFECYCLE_MIDLET_STARTED, JAVACALL_OK, NULL);

    res = runMidlet(argc, argv);
}

/**
 * The platform should invoke this function in platform context to start
 * the Java VM and run installed Java Content Handler.
 *
 * @param handlerID launched Content Handler ID
 * @param url Invocation parameter: URL
 * @param action optional Invocation parameter: Action
 *
 * @note allowed argument description can be obtained by '-help' value as arg1.
 */
void javanotify_start_handler(char* handlerID, char* url, char* action) {
    int argc = 0;
    char *argv[MIDP_RUNMIDLET_MAXIMUM_ARGS];
    javacall_result res;

    REPORT_INFO3(LC_CORE,"javanotify_start_handler() >> %s %s %s\n",
                 handlerID, url, action);

    if (initialize_memory_slavemode() != JAVACALL_OK) {
        return;
    }

    argv[argc++] = "runMidlet";
    argv[argc++] = "-1";
    argv[argc++] = "com.sun.midp.content.Invoker";
    if (NULL != handlerID) {
        argv[argc++] = handlerID;
        if (NULL != url) {
            argv[argc++] = url;
            if (NULL != action)
                argv[argc++] = action;
        }
    }

    javacall_lifecycle_state_changed(JAVACALL_LIFECYCLE_MIDLET_STARTED, JAVACALL_OK, NULL);

    res = runMidlet(argc, argv);
}

/**
 * A notification function for telling Java to perform installation of
 * a MIDlet
 *
 * If the given url is of the form http://www.sun.com/a/b/c/d.jad then
 * java will start a graphical installer will download the MIDlet
 * fom the internet.
 * If the given url is a file url (see below, file:///a/b/c/d.jar or
 * file:///a/b/c/d/jad) installation will be performed
 * in the backgroudn without launching the graphic installer application
 *
 *
 * @param url of MIDlet to install, can be either one of the following
 *   1. A full path to the jar file, of the following form file:///a/b/c/d.jar
 *   2. A full path to the JAD file, of the following form file:///a/b/c/d.jad
 *   3. An http url of jad file, of the following form,
 *      http://www.sun.com/a/b/c/d.jad
 */
void javanotify_install_midlet(const char *httpUrl) {
    int length;
    int argc = 0;
    char *argv[MIDP_RUNMIDLET_MAXIMUM_ARGS];
    javacall_result res;

    REPORT_INFO1(LC_CORE,"javanotify_install_midlet() >> httpUrl = %s\n", httpUrl);

    if (initialize_memory_slavemode() != JAVACALL_OK) {
        return;
    }

    argv[argc++] = "runMidlet";
    argv[argc++] = "-1";
    argv[argc++] = "com.sun.midp.installer.GraphicalInstaller";
    argv[argc++] = "I";

    length = strlen(httpUrl);
    if (length >= BINARY_BUFFER_MAX_LEN){
        REPORT_ERROR(LC_AMS, "javanotify_install_midlet(): httpUrl is too long\n");
        return;
    }

    memset(urlAddress, 0, BINARY_BUFFER_MAX_LEN);
    memcpy(urlAddress, httpUrl, length);
    argv[argc++] = urlAddress;

    javacall_lifecycle_state_changed(JAVACALL_LIFECYCLE_MIDLET_STARTED, JAVACALL_OK, NULL);
    res = runMidlet(argc, argv);
}

/**
 * A notification function for telling Java to perform installation of
 * a MIDlet from filesystem,
 *
 * The installation will be performed in the background without launching
 * the graphic installer application.
 *
 * The given path is the full path to MIDlet's jad file or jad.
 * In case the MIDlet's jad file is specified, then
 * the MIDlet's jar file muts reside in the same directory as the jad
 * file.
 *
 * @param jadFilePath full path the jad (or jar) file which is of the form:
 *        file://a/b/c/d.jad
 * @param jadFilePathLen length of the file path
 * @param userWasAsked a flag indicating whether the platform already asked
 *        the user for permission to download and install the application
 *        so there's no need to ask again and we can immediately install.
 */
void javanotify_install_midlet_from_filesystem(const javacall_utf16* jadFilePath,
                                               int jadFilePathLen, int userWasAsked) {
    int argc = 0;
    char *argv[MIDP_RUNMIDLET_MAXIMUM_ARGS];
    javacall_result res;
    REPORT_INFO(LC_CORE, "javanotify_install_midlet_from_filesystem() >>\n");

    JVM_Initialize();
    if (initialize_memory_slavemode() != JAVACALL_OK) {
        return;
    }

    argv[argc++] = "runMidlet";
    argv[argc++] = "-1";
    argv[argc++] = "com.sun.midp.scriptutil.CommandLineInstaller";
    argv[argc++] = "I";

    if (jadFilePathLen >= BINARY_BUFFER_MAX_LEN){
        REPORT_ERROR(LC_AMS, "javanotify_install_midlet_from_filesystem(): jadFilePathLen is too long");
        return;
    }

    memset(urlAddress, 0, BINARY_BUFFER_MAX_LEN);
    unicodeToNative(jadFilePath, jadFilePathLen,
                    (unsigned char *)urlAddress, BINARY_BUFFER_MAX_LEN);

    argv[argc++] = urlAddress;

    javacall_lifecycle_state_changed(JAVACALL_LIFECYCLE_MIDLET_STARTED, JAVACALL_OK, NULL);
    res = runMidlet(argc, argv);
}

/**
 * A notification function for telling Java to perform installation of
 * a MIDlet with parameters
 *
 * If the given url is of the form http://www.sun.com/a/b/c/d.jad then
 * java will start a graphical installer will download the MIDlet
 * fom the internet.
 * If the given url is a file url (see below, file:///a/b/c/d.jar or
 * file:///a/b/c/d/jad) installation will be performed
 * in the backgroudn without launching the graphic installer application
 *
 *
 * @param url of MIDlet to install, can be either one of the following
 *   1. A full path to the jar file, of the following form file:///a/b/c/d.jar
 *   2. A full path to the JAD file, of the following form file:///a/b/c/d.jad
 *   3. An http url of jad file, of the following form,
 *      http://www.sun.com/a/b/c/d.jad
 * @param silentInstall install the MIDlet without user interaction
 * @param forceUpdate install the MIDlet even if it already exist regardless
 *                    of version
 */
void javanotify_install_midlet_wparams(const char* httpUrl,
                                       int silentInstall, int forceUpdate) {
    int argc = 0;
    char *argv[MIDP_RUNMIDLET_MAXIMUM_ARGS];
    javacall_result res;
    int length;

    REPORT_INFO2(LC_CORE,"javanotify_install_midlet_wparams() >> "
                         "httpUrl = %s, silentInstall = %d\n",
                 httpUrl, silentInstall);

    JVM_Initialize();

    if (initialize_memory_slavemode() != JAVACALL_OK) {
        return;
    }

    argv[argc++] = "runMidlet";
    argv[argc++] = "-1";
    argv[argc++] = "com.sun.midp.installer.GraphicalInstaller";

    if (silentInstall == 1) {
        if (forceUpdate == 1) {
            argv[argc++] = "FU";
        } else {
            argv[argc++] = "FI";
        }
    } else {
        argv[argc++] = "I";
    }

    length = strlen(httpUrl);
    if (length >= BINARY_BUFFER_MAX_LEN) {
        REPORT_ERROR(LC_AMS, "javanotify_install_midlet_wparams(): httpUrl is too long");
        return;
    }

    memset(urlAddress, 0, BINARY_BUFFER_MAX_LEN);
    memcpy(urlAddress, httpUrl, length);
    argv[argc++] = urlAddress;

    javacall_lifecycle_state_changed(JAVACALL_LIFECYCLE_MIDLET_STARTED, JAVACALL_OK, NULL);
    res = runMidlet(argc, argv);
}

/**
 * The platform should invoke this function in platform context to start
 * the Java VM with arbitrary arguments.
 *
 * @param argc number of command-line arguments
 * @param argv array of command-line arguments
 *
 * @note This is a service function and it is introduced in the javacall
 *       interface for debug purposes. Please DO NOT CALL this function without
 *       being EXPLICITLY INSTRUCTED to do so.
 */
void javanotify_start_java_with_arbitrary_args(int argc, char* argv[]) {

    javacall_result res;

    REPORT_INFO(LC_CORE, "javanotify_start_java_with_arbitrary_args() >>\n");

    if (argc > MIDP_RUNMIDLET_MAXIMUM_ARGS) {
        argc = MIDP_RUNMIDLET_MAXIMUM_ARGS;
    }

    if (initialize_memory_slavemode() != JAVACALL_OK) {
        return;
    }

    javacall_lifecycle_state_changed(JAVACALL_LIFECYCLE_MIDLET_STARTED, JAVACALL_OK, NULL);
    res = runMidlet(argc, argv);
}

/**
 * Parse options for the VM
 *
 * @param argc number of command-line arguments
 * @param argv array of command-line arguments
 */
void javanotify_set_vm_args(int argc, char* argv[]) {
    midp_jc_event_union e;

    REPORT_INFO(LC_CORE, "javanotify_set_vm_args() >>\n");

    if (argc > MIDP_RUNMIDLET_MAXIMUM_ARGS) {
        argc = MIDP_RUNMIDLET_MAXIMUM_ARGS;
    }

    e.eventType = MIDP_JC_EVENT_SET_VM_ARGS;
    e.data.startMidletArbitraryArgEvent.argc = argc;
    memcpy(e.data.startMidletArbitraryArgEvent.argv, argv, argc * sizeof(char*));

    midp_jc_event_send(&e);
}

/**
 * Set VM heapsize
 * @param heapsize the heap size in bytes
 */
void javanotify_set_heap_size(int heapsize) {
    midp_jc_event_union e;

    REPORT_INFO(LC_CORE, "javanotify_set_heap_size() >>\n");

    e.eventType = MIDP_JC_EVENT_SET_HEAP_SIZE;
    e.data.heap_size.heap_size = heapsize;
    midp_jc_event_send(&e);
}

/**
 * list the MIDlet suites installed
 */
void javanotify_list_midlets(void) {
    midp_jc_event_union e;

    REPORT_INFO(LC_CORE, "javanotify_list_midlets() >>\n");

    e.eventType = MIDP_JC_EVENT_LIST_MIDLETS;
    midp_jc_event_send(&e);
}

/**
 * list the MIDlet suites installed
 * Each line contains one storage name
 */
void javanotify_list_storageNames(void) {
    midp_jc_event_union e;

    REPORT_INFO(LC_CORE, "javanotify_list_storageName() >>\n");

    e.eventType = MIDP_JC_EVENT_LIST_STORAGE_NAMES;
    midp_jc_event_send(&e);
}

/**
 * Remove a MIDlet suite according to the given suite ID
 */
void javanotify_remove_suite(char* suite_id) {
    midp_jc_event_union e;

    REPORT_INFO(LC_CORE, "javanotify_remove_suite() >>\n");

    e.eventType = MIDP_JC_EVENT_REMOVE_MIDLET;
    e.data.removeMidletEvent.suiteID = suite_id;
    midp_jc_event_send(&e);
}

/**
 * Install, run, and remove the application with the specified JAD file
 */
void javanotify_transient(char* url) {
    int length;
    midp_jc_event_union e;
    midp_jc_event_start_arbitrary_arg *data = &e.data.startMidletArbitraryArgEvent;

    REPORT_INFO(LC_CORE,"javanotify_transient() >>\n");

    e.eventType = MIDP_JC_EVENT_START_ARBITRARY_ARG;

    data->argc = 0;
    data->argv[data->argc++] = "runMidlet";
    data->argv[data->argc++] = "-1";
    data->argv[data->argc++] = "com.sun.midp.installer.AutoTester";

    length = strlen(url);
    if (length >= BINARY_BUFFER_MAX_LEN)
        return;

    memset(urlAddress, 0, BINARY_BUFFER_MAX_LEN);
    memcpy(urlAddress, url, length);
    if (strcmp(urlAddress, "none") != 0) {
        data->argv[data->argc++] = urlAddress;
    }

    data->argv[data->argc++] = "1"; /* loop count */

    midp_jc_event_send(&e);
}

/**
 * The platform should invoke this function in platform context to end Java.
 */
void javanotify_shutdown(void) {
    midp_jc_event_union e;

    REPORT_INFO(LC_CORE, "javanotify_shutdown() >>\n");

    e.eventType = MIDP_JC_EVENT_END;

    javacall_lifecycle_state_changed(JAVACALL_LIFECYCLE_MIDLET_SHUTDOWN, JAVACALL_OK, NULL);

    midp_jc_event_send(&e);
}

/**
 * The platform should invoke this function in platform context to pause
 * Java.
 */
void javanotify_pause(void) {
    midp_jc_event_union e; 

    REPORT_INFO(LC_CORE, "javanotify_pause() >>\n");

    e.eventType = MIDP_JC_EVENT_PAUSE;

    midp_jc_event_send(&e);
}

/**
 * The platform should invoke this function in platform context to end pause
 * and resume Java.
 */
void javanotify_resume(void) {
    midp_jc_event_union e; 

    REPORT_INFO(LC_CORE, "javanotify_resume() >>\n");

    e.eventType = MIDP_JC_EVENT_RESUME;

    midp_jc_event_send(&e);
}

/**
 * Decode integer parameters to locale string
 */
void decodeLanguage(char* str, short languageCode, short regionCode) {
    int i;

    str[1] = (languageCode & 0xFF);
    languageCode >>= 8;

    str[0] = (languageCode & 0xFF);
    languageCode >>= 8;

    str[2] = '-';

    str[4] = (regionCode & 0xFF);
    regionCode >>= 8;

    str[3] = (regionCode & 0xFF);
    regionCode >>= 8;

    str[5] = '\0';
}

/**
 * The platform should invoke this function for locale changing
 */
void javanotify_change_locale(short languageCode, short regionCode) {
    const char tmp[6];
    midp_jc_event_union e;
    
    REPORT_INFO(LC_CORE, "javanotify_change_locale() >>\n");

    e.eventType = MIDP_JC_EVENT_CHANGE_LOCALE;

    setSystemProperty(LOCALE, tmp);

    midp_jc_event_send(&e);
    
}

/**
 * The platform should invoke this function in platform context
 * to select another running application to be the foreground.
 */
void javanotify_select_foreground_app(void) {
#if ENABLE_MULTIPLE_ISOLATES
    midp_jc_event_union e;

    REPORT_INFO(LC_CORE, "javanotify_switchforeground() >>\n");

    e.eventType = MIDP_JC_EVENT_SWITCH_FOREGROUND;

    midp_jc_event_send(&e);
#endif /* ENABLE_MULTIPLE_ISOLATES */
}

/**
 * The platform should invoke this function in platform context
 * to bring the Application Manager Screen to foreground.
 */
void javanotify_switch_to_ams(void) {
#if ENABLE_MULTIPLE_ISOLATES
    midp_jc_event_union e;

    REPORT_INFO(LC_CORE, "javanotify_selectapp() >>\n");

    e.eventType = MIDP_JC_EVENT_SELECT_APP;

    midp_jc_event_send(&e);
#endif /* ENABLE_MULTIPLE_ISOLATES */
}

/**
 * The platform should invoke this function in platform context to pause
 * Java bytecode execution (without invoking pauseApp)
 */
void javanotify_internal_pause(void) {
    midp_jc_event_union e;

    REPORT_INFO(LC_CORE, "javanotify_internal_pause() >>\n");

    e.eventType = MIDP_JC_EVENT_INTERNAL_PAUSE;

    midp_jc_event_send(&e);
}

/**
 * The platform should invoke this function in platform context to end
 * an internal pause and resume Java bytecode processing
 */
void javanotify_internal_resume(void) {
    midp_jc_event_union e;

    REPORT_INFO(LC_CORE, "javanotify_internal_resume() >>\n");

    e.eventType = MIDP_JC_EVENT_INTERNAL_RESUME;

    midp_jc_event_send(&e);
}

/**
 * A notification function for telling Java to perform installation of
 * a content via http, for SprintAMS.
 *
 * This function requires that the descriptor (JADfile, or GCDfile)
 * has already been downloaded and resides somewhere on the file system.
 * The function also requires the full URL that was used to download the
 * file.
 *
 * The given URL should be of the form http://www.sun.com/a/b/c/d.jad
 * or http://www.sun.com/a/b/c/d.gcd.
 * Java will start a graphical installer which will download the content
 * fom the Internet.
 *
 * @param httpUrl null-terminated http URL string of the content
 *        descriptor. The URL is of the following form:
 *        http://www.website.com/a/b/c/d.jad
 * @param descFilePath full path of the descriptor file which is of the
 *        form:
 *        /a/b/c/d.jad  or /a/b/c/d.gcd
 * @param descFilePathLen length of the file path
 * @param isJadFile set to TRUE if the mime type of of the downloaded
 *        descriptor file is <tt>text/vnd.sun.j2me.app-descriptor</tt>. If
 *        the mime type is anything else (e.g., <tt>text/x-pcs-gcd</tt>),
 *        this must be set to FALSE.
 * @param isSilent set to TRUE if the content is to be installed silently,
 *        without intervention from the user. (e.g., in the case of SL
 *        or SI messages)
 *
 */
void javanotify_install_content(const char * httpUrl,
                                const javacall_utf16* descFilePath,
                                unsigned int descFilePathLen,
                                javacall_bool isJadFile,
                                                                javacall_bool isSilent) {
    const static int SchemaLen = 16;
    const static javacall_utf16 SchemaFile[] = {'f','i','l','e',':','/','/','/'};

    midp_jc_event_union e;
    int httpUrlLength, dscFileOffset;

    REPORT_INFO(LC_CORE, "javanotify_install_content() >>\n");

    if ((httpUrl == NULL) || (httpUrl == NULL)) {
        return; /* mandatory parameter is NULL */
    }

    httpUrlLength = strlen(httpUrl) + 1;
    dscFileOffset = (httpUrlLength + 1) & (-2); /* align to WORD boundary */

    if ((descFilePathLen <= 0) ||
        (descFilePathLen * 2 + dscFileOffset > sizeof(urlAddress))) {
        return; /* static buffer so small */
    }

    /* Is it nessasary to add schema file like: "file:///"? */
    /* Let move the input strings to the static buffer - urlAddress */
    memcpy(urlAddress, httpUrl, httpUrlLength);
    memcpy(urlAddress + dscFileOffset, descFilePath, descFilePathLen * 2);

    e.eventType = MIDP_JC_EVENT_INSTALL_CONTENT;
    e.data.install_content.httpUrl = urlAddress;
    e.data.install_content.descFilePath = (javacall_utf16*) urlAddress + dscFileOffset;
    e.data.install_content.descFilePathLen = descFilePathLen;
    e.data.install_content.isJadFile = isJadFile;
    e.data.install_content.isSilent = isSilent;

    midp_jc_event_send(&e);
}
