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

Handle<Value> GetModules(const Arguments& args);
void AsyncGetModules(uv_work_t* req);

Handle<Value> InstallModule(const Arguments& args);
void AsyncInstallModule(uv_work_t* req);

Handle<Value> GetModuleBCV(const Arguments& args);
void AsyncGetModuleBCV(uv_work_t* req);

Handle<Value> GetRawText(const Arguments& args);
void AsyncGetRawText(uv_work_t* req);

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
    bool asyncSend;
    std::string error_message;
    std::string result;
    std::string arg1;
    std::string arg2;
    bool argBool;
};

uv_loop_t *loop;
uv_async_t async;

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
    displayLibrary = new SWMgr(new MarkupFilterMgr(FMT_HTML));
    searchLibrary = new SWMgr();
    displayLibrary->setGlobalOption("Footnotes","On");
    displayLibrary->setGlobalOption("Headings", "On");
    displayLibrary->setGlobalOption("Strong's Numbers", "On");
    displayLibrary->setGlobalOption("Words of Christ in Red", "On");
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
            //async.data = out.str();
            //uv_async_send(&async);
            std::cout << out.str() << std::endl;

            //TODO: Need a callback here do report the status.
        }
};

void initInstallMgr() {
    //putenv("SWORD_PATH=/home/zefanja/.sword");
    if (!mgr) {
        mgr = new SWMgr();

        if (!mgr->config) {
            std::cout << "ERROR: SWORD configuration not found.  Please configure SWORD before using this program.";
            return;
        }
        SWBuf baseDir = mgr->getHomeDir();
        if (baseDir.length() < 1) baseDir = ".";
        baseDir += "/.sword/InstallMgr";
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


void SwordRemoteInstallModule(Baton *baton, const char* sourceName, const char* modName) {
    initInstallMgr();
    InstallSourceMap::iterator source = installMgr->sources.find(sourceName);
    if (source == installMgr->sources.end()) {
        baton->error_message = "Couldn't find remote source";
        baton->error = true;
        return;
    }
    InstallSource *is = source->second;
    SWMgr *rmgr = is->getMgr();
    SWModule *module;
    ModMap::iterator it = rmgr->Modules.find(modName);
    if (it == rmgr->Modules.end()) {
        baton->error_message = "Remote source does not have this module";
        baton->error = true;
        return;
    }
    module = it->second;

    int error = installMgr->installModule(mgr, 0, module->Name(), is);
    if (error) {
        baton->error_message = "Error installing module. (internet connection?)";
        baton->error = true;
        return;
    }

    //Refresh Mgr
    refreshManagers();
}

//END INSTALL MANAGER STUFF

std::string SwordGetModuleBCV(Baton *baton, const char* moduleName) {
    std::stringstream bnames;

    SWModule *module = displayLibrary->getModule(moduleName);
    if(!module) {
        baton->error = true;
        baton->error_message = "Module does not exist";
        return "";
    }

    VerseKey *vkey = (VerseKey*)module->getKey();
    if(!vkey) {
        baton->error = true;
        baton->error_message = "Couldn't find vkey";
        return "";
    }

    VerseKey &vk = *vkey;

    bnames << "[";
    for (int b = 0; b < 2; b++) {
        vk.setTestament(b+1);
        for (int i = 0; i < vk.BMAX[b]; i++) {
            vk.setBook(i+1);
            bnames << "{\"name\": \"" << convertString(vk.getBookName()) << "\", ";
            bnames << "\"abbrev\": \"" << convertString(vk.getBookAbbrev()) << "\", ";
            bnames << "\"vmax\": [";
            for (int c = 0; c<vk.getChapterMax(); c++) {
                vk.setChapter(c+1);
                bnames << vk.getVerseMax();
                if (c+1 < vk.getChapterMax()) {
                    bnames << ", ";
                }

            }
            bnames << "], \"cmax\":" << vk.getChapterMax() << "}";
            if (i+1 == vk.BMAX[b] && b == 1) {
                bnames << "]";
            } else {
                bnames << ", ";
            }
        }
    }

    return bnames.str();
}

std::string SwordGetRawText(Baton *baton, const char* moduleName, const char* key) {
    /*Get verses from a specific module (e.g. "ESV"). Set your biblepassage in key e.g. "James 1:19" */
    std::stringstream passage;
    std::stringstream tmpPassage;
    std::stringstream out;

    SWModule *module = displayLibrary->getModule(moduleName);
    if(!module) {
        baton->error = true;
        baton->error_message = "Module does not exist";
        return "";
    }

    VerseKey *vk = (VerseKey*)module->getKey();
    vk->Headings(true);
    ListKey verses = VerseKey().ParseVerseList(key, "", true);

    AttributeTypeList::iterator i1;
    AttributeList::iterator i2;
    AttributeValue::iterator i3;

    out << "[";

    for (verses = TOP; !verses.Error(); verses++) {
        vk->setText(verses);

        if (strcmp(module->RenderText(), "") != 0) {
            //headingOn = 0;
            out << "{\"content\": \"" << convertString(module->RenderText()) << "\"";
            out << ", \"vnumber\": \"" << vk->Verse() << "\"";
            out << ", \"cnumber\": \"" << vk->Chapter() << "\"";
            out << ", \"osisRef\": \"" << vk->getOSISRef() << "\"";
            out << ", \"osisBook\": \"" << vk->getOSISBookName() << "\"";
            if (strcmp(module->getEntryAttributes()["Heading"]["Preverse"]["0"].c_str(), "") != 0)
                out << ", \"heading\": \"" << module->getEntryAttributes()["Heading"]["Preverse"]["0"].c_str() << "\"";

            for (i1 = module->getEntryAttributes().begin(); i1 != module->getEntryAttributes().end(); i1++) {
                if (strcmp(i1->first, "Footnote") == 0) {
                    out << ", \"footnotes\": [";
                    for (i2 = i1->second.begin(); i2 != i1->second.end(); i2++) {
                        out << "{";
                        for (i3 = i2->second.begin(); i3 != i2->second.end(); i3++) {
                            out << "\"" << i3->first << "\": \"" << convertString(i3->second.c_str()) << "\"";
                            //footnotesOn = 1;
                            if (i3 != --i2->second.end()) {
                                out << ", ";
                            }
                        }
                        out << "}";
                        if (i2 != --i1->second.end()) {
                            out << ", ";
                        }
                    }
                    out << "]";
                }
            }

            if (vk->Chapter() == 1 && vk->Verse() == 1) {
                vk->setChapter(0);
                vk->setVerse(0);
                out << ", \"intro\": \"" << convertString(module->RenderText()) << "\"";
            }

            out << "}";

            ListKey helper = verses;
            helper++;
            if (!helper.Error()) {
                out << ", ";
            }
        }
    }

    out << "]";
    return out.str();
}

//BEGIN NODE WRAPPER STUFF

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
        } else {
            return ThrowException(Exception::TypeError(
                String::New("refresh is not defined")));
        }
        if (opt->Has(String::New("sourceName"))) {
            if ((opt->Get(String::New("sourceName"))->IsString())){
                Handle<Value> _sourceName = opt->Get(String::New("sourceName"));
                baton->arg1 = cv::CastFromJS<std::string>(_sourceName);

            } else {
                return ThrowException(Exception::TypeError(
                    String::New("sourceName must be a string")));
            }
        } else {
            return ThrowException(Exception::TypeError(
                String::New("sourceName is not defined")));
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

Handle<Value> GetModules(const Arguments& args) {
    //Get a list of all modules
    HandleScope scope;
    Baton* baton = new Baton();

    if (!args[0]->IsFunction()) {
        return ThrowException(Exception::TypeError(
            String::New("First argument must be a callback function")));
    }

    Local<Function> callback = Local<Function>::Cast(args[0]);
    baton->error = false;
    baton->callback = Persistent<Function>::New(callback);

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    /*loop = uv_default_loop();
    uv_async_init(loop, &async, print_progress);
    baton->asyncSend = true; */

    int status = uv_queue_work(uv_default_loop(), req, AsyncGetModules,
                               (uv_after_work_cb)AfterAsyncWork);
    assert(status == 0);

    return Undefined();
}



void AsyncGetModules(uv_work_t* req) {
    Baton* baton = static_cast<Baton*>(req->data);
    baton->result = SwordListModules();
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

Handle<Value> InstallModule(const Arguments& args) {
    //install a module
    HandleScope scope;
    Baton* baton = new Baton();

    if (args[0]->IsObject()) {
        Handle<Object> opt = Handle<Object>::Cast(args[0]);
        if (opt->Has(String::New("moduleName"))) {
            if ((opt->Get(String::New("moduleName"))->IsString())){
                Handle<Value> _moduleName = opt->Get(String::New("moduleName"));
                baton->arg1 = cv::CastFromJS<std::string>(_moduleName);

            } else {
                return ThrowException(Exception::TypeError(
                    String::New("moduleName must be a string")));
            }
        }
        if (opt->Has(String::New("sourceName"))) {
            if ((opt->Get(String::New("sourceName"))->IsString())){
                Handle<Value> _sourceName = opt->Get(String::New("sourceName"));
                baton->arg2 = cv::CastFromJS<std::string>(_sourceName);

            } else {
                return ThrowException(Exception::TypeError(
                    String::New("sourceName must be a string")));
            }
        }
    } else {
        return ThrowException(Exception::TypeError(
            String::New("First argument must be an object like {sourceName: 'foo', moduleName: 'bar'")));
    }

    if (!args[1]->IsFunction()) {
        return ThrowException(Exception::TypeError(
            String::New("Second argument must be a callback function")));
    }

    Local<Function> callback = Local<Function>::Cast(args[1]);
    baton->error = false;
    baton->callback = Persistent<Function>::New(callback);

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    loop = uv_default_loop();
    //uv_async_init(loop, &async, print_progress);
    //baton->asyncSend = true;

    int status = uv_queue_work(loop, req, AsyncInstallModule,
                               (uv_after_work_cb)AfterAsyncWork);
    assert(status == 0);

    return Undefined();
}

void AsyncInstallModule(uv_work_t* req) {
    Baton* baton = static_cast<Baton*>(req->data);
    SwordRemoteInstallModule(baton, baton->arg2.c_str(), baton->arg1.c_str());
}

Handle<Value> GetModuleBCV(const Arguments& args) {
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

    int status = uv_queue_work(uv_default_loop(), req, AsyncGetModuleBCV,
                               (uv_after_work_cb)AfterAsyncWork);
    assert(status == 0);

    return Undefined();
}

void AsyncGetModuleBCV(uv_work_t* req) {
    Baton* baton = static_cast<Baton*>(req->data);
    baton->result = SwordGetModuleBCV(baton, baton->arg1.c_str());
}

Handle<Value> GetRawText(const Arguments& args) {
    //Get raw text entry specified by a vkey
    HandleScope scope;
    Baton* baton = new Baton();

    if (args[0]->IsObject()) {
        Handle<Object> opt = Handle<Object>::Cast(args[0]);
        if (opt->Has(String::New("moduleName"))) {
            if ((opt->Get(String::New("moduleName"))->IsString())){
                Handle<Value> _moduleName = opt->Get(String::New("moduleName"));
                baton->arg1 = cv::CastFromJS<std::string>(_moduleName);

            } else {
                return ThrowException(Exception::TypeError(
                    String::New("moduleName must be a string")));
            }
        }
        if (opt->Has(String::New("key"))) {
            if ((opt->Get(String::New("key"))->IsString())){
                Handle<Value> _key = opt->Get(String::New("key"));
                baton->arg2 = cv::CastFromJS<std::string>(_key);

            } else {
                return ThrowException(Exception::TypeError(
                    String::New("key must be a string")));
            }
        }
    } else {
        return ThrowException(Exception::TypeError(
            String::New("First argument must be an object like {key: 'foo', moduleName: 'bar'")));
    }

    if (!args[1]->IsFunction()) {
        return ThrowException(Exception::TypeError(
            String::New("Second argument must be a callback function")));
    }

    Local<Function> callback = Local<Function>::Cast(args[1]);
    baton->error = false;
    baton->callback = Persistent<Function>::New(callback);

    uv_work_t *req = new uv_work_t();
    req->data = baton;

    int status = uv_queue_work(uv_default_loop(), req, AsyncGetRawText,
                               (uv_after_work_cb)AfterAsyncWork);
    assert(status == 0);

    return Undefined();
}

void AsyncGetRawText(uv_work_t* req) {
    Baton* baton = static_cast<Baton*>(req->data);
    baton->result = SwordGetRawText(baton, baton->arg1.c_str(), baton->arg2.c_str());
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
    if (baton->asyncSend)
        uv_close((uv_handle_t*) &async, NULL);
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
    exports->Set(String::NewSymbol("getModules"),
        FunctionTemplate::New(GetModules)->GetFunction());
    exports->Set(String::NewSymbol("installModule"),
        FunctionTemplate::New(InstallModule)->GetFunction());
    exports->Set(String::NewSymbol("getModuleBCV"),
        FunctionTemplate::New(GetModuleBCV)->GetFunction());
    exports->Set(String::NewSymbol("getRawText"),
        FunctionTemplate::New(GetRawText)->GetFunction());

    //Init InstallManager and a ModuleManager
    initInstallMgr();
    refreshManagers();
}

NODE_MODULE(sword_addon, Init)
