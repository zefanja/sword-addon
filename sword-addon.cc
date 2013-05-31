// BEGIN LICENSE
// Copyright (C) 2013 Stephan Tetzel <info@zefanjas.de>
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License version 3, as published
// by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranties of
// MERCHANTABILITY, SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR
// PURPOSE.  See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program.  If not, see <http://www.gnu.org/licenses/>.
// END LICENSE

//#define BUILDING_NODE_EXTENSION
#include <node.h>
#include <cvv8/convert.hpp>

#include <stdio.h>
#include <string>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <vector>
#include <algorithm>
#include <iterator>

#include <regex.h>
#include <pthread.h>

/*SWORD HEADER */
#include <swmgr.h>
#include <swmodule.h>
#include <markupfiltmgr.h>
#include <listkey.h>
#include <versekey.h>
#include <swlocale.h>
#include <localemgr.h>
#include <installmgr.h>
#include <ftptrans.h>
#include <filemgr.h>

namespace cv = cvv8;
using namespace v8;
using namespace sword;

// Forward declaration. Usually, you do this in a header file.
Handle<Value> SyncRemoteSources(const Arguments& args);
void AsyncSyncRemoteSources(uv_work_t* req);
void AfterAsyncWork(uv_work_t* req);

Handle<Value> GetRemoteSources(const Arguments& args);
void AsyncGetRemoteSources(uv_work_t* req);

Handle<Value> RefreshRemoteSource(const Arguments& args);
void AsyncRefreshRemoteSource(uv_work_t* req);

Handle<Value> GetRemoteModules(const Arguments& args);
void AsyncGetRemoteModules(uv_work_t* req);

std::string SwordListModules(SWMgr *otherMgr, bool onlyNewAndUpdates);

SWMgr *displayLibrary = 0;
SWMgr *searchLibrary = 0;

std::string searchModule = "";
std::string searchTerm = "";
std::string searchScope = "";
std::string verseView = "";
std::string remoteSource = "";
std::string modName = "";
int searchType = -2;

struct Baton {
    Persistent<Function> callback;
    bool error;
    std::string error_message;
    std::string result;
    std::string arg1;
    std::string arg2;
    bool argBool;
};

std::string convertString(std::string s) {
    std::stringstream ss;
    for (size_t i = 0; i < s.length(); ++i) {
        if (unsigned(s[i]) < '\x20' || s[i] == '\\' || s[i] == '"') {
            ss << "\\u" << std::setfill('0') << std::setw(4) << std::hex << unsigned(s[i]);
        } else {
            ss << s[i];
        }
    }
    return ss.str();
}

void refreshManagers() {
    delete displayLibrary;
    delete searchLibrary;
    displayLibrary = new SWMgr(new MarkupFilterMgr(FMT_HTMLHREF));
    searchLibrary = new SWMgr();
    displayLibrary->setGlobalOption("Footnotes","On");
    displayLibrary->setGlobalOption("Headings", "On");
}

/*INSTALL MANAGER STUFF */

SWMgr *mgr = 0;
InstallMgr *installMgr = 0;
StatusReporter *statusReporter = 0;
SWBuf baseDir;
SWBuf confPath;

class MyInstallMgr : public InstallMgr {
public:
    MyInstallMgr(const char *privatePath = "./", StatusReporter *sr = 0) : InstallMgr(privatePath, sr) {}

virtual bool isUserDisclaimerConfirmed() const {
    //Do this in the frontend
    return true;
}
};

class MyStatusReporter : public StatusReporter {
    int last;
        virtual void preStatus(long totalBytes, long completedBytes, const char *message) {
            std::stringstream out;

            out << "{\"total\": \"" << totalBytes << "\", \"completed\": \"" << completedBytes << "\"}";
            //TODO: Need a callback here do report the status.
        }
};

void initInstallMgr() {
    if (!mgr) {
        mgr = new SWMgr();

        if (!mgr->config)
            std::cout << "ERROR: SWORD configuration not found.  Please configure SWORD before using this program.";

        SWBuf baseDir = ""; //TODO: Do we need to set a path here or just take the current path? Best would be $HOME.
        if (baseDir.length() < 1) baseDir = ".";
        baseDir += "/.sword/InstallMgr";
        //PDL_Log("HELLO " + baseDir.c_str());
        confPath = baseDir + "/InstallMgr.conf";
        statusReporter = new MyStatusReporter();
        installMgr = new MyInstallMgr(baseDir, statusReporter);
    }
}

// clean up and exit if status is 0 or negative error code
void finish(int status) {
    delete statusReporter;
    delete installMgr;
    delete mgr;

    installMgr = 0;
    mgr        = 0;

    if (status < 1) {
        std::cout << "\n";
        exit(status);
    }
}

void createBasicConfig(bool enableRemote, bool addCrossWire) {
    FileMgr::createParent(confPath.c_str());
    remove(confPath.c_str());

    InstallSource is("FTP");
    is.caption = "CrossWire";
    is.source = "ftp.crosswire.org";
    is.directory = "/pub/sword/raw";

    SWConfig config(confPath.c_str());
    config["General"]["PassiveFTP"] = "true";
    if (enableRemote) {
        config["Sources"]["FTPSource"] = is.getConfEnt();
    }
    config.Save();
}

/*

PDL_bool uninstallModule(PDL_JSParameters *parms) {
    //void uninstallModule(const char *modName) {
    initInstallMgr();
    const char* modName = PDL_GetJSParamString(parms, 0);
    std::stringstream out;
    SWModule *module;
    ModMap::iterator it = searchLibrary->Modules.find(modName);
    if (it == searchLibrary->Modules.end()) {
        PDL_JSException(parms, "uninstallModule: Couldn't find module");
        finish(-2);
        return PDL_FALSE;
    }
    module = it->second;
    installMgr->removeModule(searchLibrary, module->Name());
    out << "{\"returnValue\": true, \"message\": \"Removed module\"}";

    //Refresh Mgr
    refreshManagers();

    const std::string& tmp = out.str();

    PDL_Err mjErr = PDL_JSReply(parms, tmp.c_str());
    return PDL_TRUE;
} */

std::string SwordSyncRemoteSources(Baton *baton) {
    std::string sources;
    // be sure we have at least some config file already out there
    if (!FileMgr::existsFile(confPath.c_str())) {
        createBasicConfig(true, true);
        finish(1); // cleanup and don't exit
        initInstallMgr();    // re-initInstallMgr with InstallMgr which uses our new config
    }

    if (!installMgr->refreshRemoteSourceConfiguration())
        sources = "{\"message\": \"successfully synced remote source configuration.\"}";
    else {
        baton->error_message = "Could not sync remote source configuration. Check your Internet connection!";
        baton->error = true;
    }

    return sources;
}

std::string SwordListRemoteSources() {
    std::stringstream sources;
    sources << "[";
    for (InstallSourceMap::iterator it = installMgr->sources.begin(); it != installMgr->sources.end(); it++) {
        if (it != installMgr->sources.begin()) {
            sources << ", ";
        }
        sources << "{\"name\": \"" << it->second->caption << "\", ";
        sources << "\"type\": \"" << it->second->type << "\", ";
        sources << "\"source\": \"" << it->second->source << "\", ";
        sources << "\"directory\": \"" << it->second->directory << "\"}";
    }
    sources << "]";

    return sources.str();
}

void SwordRefreshRemoteSource(Baton *baton, const char *sourceName) {
    std::stringstream out;
    InstallSourceMap::iterator source = installMgr->sources.find(sourceName);
    if (source == installMgr->sources.end()) {
        baton->error_message = "Couldn't find remote source";
        baton->error = true;
    }

    if (!installMgr->refreshRemoteSource(source->second))
        baton->error_message =  "Error Refreshing Remote Source";
}

std::string SwordListRemoteModules(Baton *baton, const char* sourceName, bool refreshSource = true) {
    bool onlyNewAndUpdated = false;
    InstallSourceMap::iterator source = installMgr->sources.find(sourceName);
    if (source == installMgr->sources.end()) {
        baton->error_message = "remoteListModules: Couldn't find remote source";
        baton->error = true;
    }

    if(refreshSource) {
        SwordRefreshRemoteSource(baton, sourceName);
    }

    return SwordListModules(source->second->getMgr(), onlyNewAndUpdated);
}


std::string SwordListModules(SWMgr *otherMgr = 0, bool onlyNewAndUpdates = false) {
    std::stringstream out;
    SWModule *module;
    if (!otherMgr) otherMgr = mgr;
    std::map<SWModule *, int> mods = InstallMgr::getModuleStatus(*mgr, *otherMgr);

    out << "[";

    for (std::map<SWModule *, int>::iterator it = mods.begin(); it != mods.end(); it++) {
        module = it->first;
        SWBuf version = module->getConfigEntry("Version");
        SWBuf status = " ";
        if (it->second & InstallMgr::MODSTAT_NEW) status = "*";
        if (it->second & InstallMgr::MODSTAT_OLDER) status = "-";
        if (it->second & InstallMgr::MODSTAT_UPDATED) status = "+";

        if (!onlyNewAndUpdates || status == "*" || status == "+") {
            if (it != mods.begin()) {
                out << ", ";
            }
            out << "{\"name\": \"" << module->Name() << "\"";
            out << ", \"datapath\": \"" << module->getConfigEntry("DataPath") << "\"";
            out << ", \"description\": \"" << module->getConfigEntry("Description") << "\"";
            if (module->getConfigEntry("Lang")) out << ", \"lang\": \"" << module->getConfigEntry("Lang") << "\"";
            if (module->getConfigEntry("Versification")) out << ", \"versification\": \"" << module->getConfigEntry("Versification") << "\"";
            if (module->getConfigEntry("About")) out << ", \"about\": \"" << convertString(module->getConfigEntry("About")) << "\"";
            if (module->getConfigEntry("Version")) out << ", \"version\": \"" << module->getConfigEntry("Version") << "\"";
            if (module->getConfigEntry("InstallSize")) out << ", \"installSize\": \"" << module->getConfigEntry("InstallSize") << "\"";
            if (module->getConfigEntry("Copyright")) out << ", \"copyright\": \"" << convertString(module->getConfigEntry("Copyright")) << "\"";
            if (module->getConfigEntry("DistributionLicense")) out << ", \"distributionLicense\": \"" << module->getConfigEntry("DistributionLicense") << "\"";
            if (module->getConfigEntry("Category")) out << ", \"category\": \"" << module->getConfigEntry("Category") << "\"";
            out << "}";
        }
    }
    out << "]";

    return out.str();
}


/*void *remoteInstallModule(void *foo) {
//void remoteInstallModule(const char *sourceName, const char *modName) {
    initInstallMgr();
    std::stringstream out;
    InstallSourceMap::iterator source = installMgr->sources.find(remoteSource.c_str());
    if (source == installMgr->sources.end()) {
        out << "{\"returnValue\": false, \"message\": \"Couldn't find remote source: " << remoteSource << "\"}";
        finish(-3);
    }
    InstallSource *is = source->second;
    SWMgr *rmgr = is->getMgr();
    SWModule *module;
    ModMap::iterator it = rmgr->Modules.find(modName.c_str());
    if (it == rmgr->Modules.end()) {
        out << "{\"returnValue\": false, \"message\": \"Remote source " << remoteSource << " does not make available module " << modName << "\"}";
        finish(-4);
    }
    module = it->second;

    int error = installMgr->installModule(mgr, 0, module->Name(), is);
    if (error) {
        out << "{\"returnValue\": false, \"message\": \"Error installing module: " << modName << ". (internet connection?)\"}";
    } else out << "{\"returnValue\": true, \"message\": \"Installed module: " << modName << "\"}";

    //Refresh Mgr
    refreshManagers();

    const std::string& tmp = out.str();
    const char* cstr = tmp.c_str();

    const char *params[1];
    params[0] = cstr;
    PDL_Err mjErr = PDL_CallJS("returnUnzip", params, 1);
}

PDL_bool callRemoteInstallModule(PDL_JSParameters *parms) {
    const char* sourceName = PDL_GetJSParamString(parms, 0);
    const char* moduleName = PDL_GetJSParamString(parms, 1);
    pthread_t thread1;
    int  iret1;

    char *foobar;
    remoteSource = sourceName;
    modName = moduleName;

    iret1 = pthread_create( &thread1, NULL, remoteInstallModule, (void *) foobar);
    return PDL_TRUE;
} */

//END INSTALL MANAGER STUFF

Handle<Value> SyncRemoteSources(const Arguments& args) {
    HandleScope scope;

    if (!args[0]->IsFunction()) {
        return ThrowException(Exception::TypeError(
            String::New("First argument must be a callback function")));
    }

    Local<Function> callback = Local<Function>::Cast(args[0]);

    // The baton holds our custom status information for this asynchronous call,
    // like the callback function we want to call when returning to the main
    // thread and the status information.
    Baton* baton = new Baton();
    baton->error = false;
    baton->callback = Persistent<Function>::New(callback);

    // This creates the work request struct.
    uv_work_t *req = new uv_work_t();
    req->data = baton;

    // Schedule our work request with libuv. Here you can specify the functions
    // that should be executed in the threadpool and back in the main thread
    // after the threadpool function completed.
    int status = uv_queue_work(uv_default_loop(), req, AsyncSyncRemoteSources,
                               (uv_after_work_cb)AfterAsyncWork);
    assert(status == 0);

    return Undefined();
}

void AsyncSyncRemoteSources(uv_work_t* req) {
    Baton* baton = static_cast<Baton*>(req->data);
    baton->result = SwordSyncRemoteSources(baton);
}

Handle<Value> GetRemoteSources(const Arguments& args) {
    HandleScope scope;

    if (!args[0]->IsFunction()) {
        return ThrowException(Exception::TypeError(
            String::New("First argument must be a callback function")));
    }

    Local<Function> callback = Local<Function>::Cast(args[0]);

    Baton* baton = new Baton();
    baton->error = false;
    baton->callback = Persistent<Function>::New(callback);

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    int status = uv_queue_work(uv_default_loop(), req, AsyncGetRemoteSources,
                               (uv_after_work_cb)AfterAsyncWork);
    assert(status == 0);

    return Undefined();
}

void AsyncGetRemoteSources(uv_work_t* req) {
    Baton* baton = static_cast<Baton*>(req->data);
    baton->result = SwordListRemoteSources();
}

Handle<Value> GetRemoteModules(const Arguments& args) {
    //Get a list of all modules form a source.
    //arg1 - Object like {sourceName: "foo", refresh: false}. Refresh will be true by default.
    //arg2 - callback function
    HandleScope scope;
    Baton* baton = new Baton();
    bool Refresh = true;

    if (args[0]->IsObject()) {
        Handle<Object> opt = Handle<Object>::Cast(args[0]);
        if (opt->Has(String::New("refresh"))) {
            if ((opt->Get(String::New("refresh"))->IsBoolean())){
                Handle<Value> _refresh = opt->Get(String::New("refresh"));
                Refresh = cv::CastFromJS<bool>(_refresh);

            } else {
                return ThrowException(Exception::TypeError(
                    String::New("refresh must be a bool")));
            }
        }
        if (opt->Has(String::New("sourceName"))) {
            if ((opt->Get(String::New("sourceName"))->IsString())){
                Handle<Value> _sourceName = opt->Get(String::New("sourceName"));
                baton->arg1 = cv::CastFromJS<std::string>(_sourceName);

            } else {
                return ThrowException(Exception::TypeError(
                    String::New("refresh must be a string")));
            }
        }
    } else {
        return ThrowException(Exception::TypeError(
            String::New("First argument must be an object like {sourceName: 'foo', refresh: true")));
    }

    if (!args[1]->IsFunction()) {
        return ThrowException(Exception::TypeError(
            String::New("Second argument must be a callback function")));
    }

    Local<Function> callback = Local<Function>::Cast(args[1]);
    baton->error = false;
    baton->callback = Persistent<Function>::New(callback);
    baton->argBool = Refresh;

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    int status = uv_queue_work(uv_default_loop(), req, AsyncGetRemoteModules,
                               (uv_after_work_cb)AfterAsyncWork);
    assert(status == 0);

    return Undefined();
}

void AsyncGetRemoteModules(uv_work_t* req) {
    Baton* baton = static_cast<Baton*>(req->data);
    const char* sourceName = baton->arg1.c_str();
    bool refresh = baton->argBool;

    baton->result = SwordListRemoteModules(baton, sourceName, refresh);
}

Handle<Value> RefreshRemoteSource(const Arguments& args) {
    HandleScope scope;

    if (!args[0]->IsString()) {
        ThrowException(Exception::TypeError(String::New("First argument must be a string!")));
        return scope.Close(Undefined());
    }

    if (!args[1]->IsFunction()) {
        return ThrowException(Exception::TypeError(
            String::New("Second argument must be a callback function")));
    }

    v8::String::Utf8Value param1(args[0]->ToString());

    Local<Function> callback = Local<Function>::Cast(args[1]);

    Baton* baton = new Baton();
    baton->error = false;
    baton->callback = Persistent<Function>::New(callback);
    baton->arg1 = std::string(*param1);

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    int status = uv_queue_work(uv_default_loop(), req, AsyncRefreshRemoteSource,
                               (uv_after_work_cb)AfterAsyncWork);
    assert(status == 0);

    return Undefined();
}

void AsyncRefreshRemoteSource(uv_work_t* req) {
    Baton* baton = static_cast<Baton*>(req->data);
    const char* sourceName = baton->arg1.c_str();

    SwordRefreshRemoteSource(baton, sourceName);
}

void AfterAsyncWork(uv_work_t* req) {
    HandleScope scope;
    Baton* baton = static_cast<Baton*>(req->data);

    if (baton->error) {
        Local<Value> err = Exception::Error(String::New(baton->error_message.c_str()));
        const unsigned argc = 1;
        Local<Value> argv[argc] = { err };
        TryCatch try_catch;
        baton->callback->Call(Context::GetCurrent()->Global(), argc, argv);
        if (try_catch.HasCaught()) {
            node::FatalException(try_catch);
        }
    } else {
        const unsigned argc = 2;
        Local<Value> argv[argc] = {
            Local<Value>::New(Null()),
            Local<Value>::New(String::New(baton->result.c_str()))
        };
        TryCatch try_catch;
        baton->callback->Call(Context::GetCurrent()->Global(), argc, argv);
        if (try_catch.HasCaught()) {
            node::FatalException(try_catch);
        }
    }
    baton->callback.Dispose();
    delete baton;
    delete req;
}

void Init(Handle<Object> exports, Handle<Object> module) {
    exports->Set(String::NewSymbol("syncRemoteSources"),
        FunctionTemplate::New(SyncRemoteSources)->GetFunction());
    exports->Set(String::NewSymbol("getRemoteSources"),
        FunctionTemplate::New(GetRemoteSources)->GetFunction());
    exports->Set(String::NewSymbol("refreshRemoteSource"),
        FunctionTemplate::New(RefreshRemoteSource)->GetFunction());
    exports->Set(String::NewSymbol("getRemoteModules"),
        FunctionTemplate::New(GetRemoteModules)->GetFunction());

    //Init InstallManager and a ModuleManager
    initInstallMgr();
    refreshManagers();
}

NODE_MODULE(sword_addon, Init)
