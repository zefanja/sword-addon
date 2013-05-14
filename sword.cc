//#define BUILDING_NODE_EXTENSION
#include <node.h>
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

using namespace v8;
using namespace sword;

SWMgr *displayLibrary = 0;
SWMgr *searchLibrary = 0;

std::string searchModule = "";
std::string searchTerm = "";
std::string searchScope = "";
std::string verseView = "";
std::string remoteSource = "";
std::string modName = "";
int searchType = -2;

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

void usage(const char *progName = 0, const char *error = 0);

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
        virtual void statusUpdate(double dltotal, double dlnow) {
            /*int p = 74 * (int)(dlnow / dltotal);
            for (;last < p; ++last) {
                if (!last) {
                    SWBuf output;
                    output.setFormatted("[ File Bytes: %ld", (long)dltotal);
                    while (output.size() < 75) output += " ";
                    output += "]";
                    std::cout << output.c_str() << "\n ";
                }
                std::cout << "-";
            }
            std::cout.flush(); */
        }

        virtual void preStatus(long totalBytes, long completedBytes, const char *message) {
            std::stringstream out;

            out << "{\"total\": \"" << totalBytes << "\", \"completed\": \"" << completedBytes << "\"}";
            //TODO: Need a callback here do report the status.
        }
};

void init() {
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

void initConfig() {
    init();
    bool enable = true; //installMgr->isUserDisclaimerConfirmed();
    createBasicConfig(enable, true);
}

void *syncConfig(void *foo) {
//int syncConfig() {
    std::stringstream sources;
    init();

    // be sure we have at least some config file already out there
    if (!FileMgr::existsFile(confPath.c_str())) {
        createBasicConfig(true, true);
        finish(1); // cleanup and don't exit
        init();    // re-init with InstallMgr which uses our new config
    }

    if (!installMgr->refreshRemoteSourceConfiguration())
        sources << "{\"returnValue\": true}";
    else
        sources << "{\"returnValue\": false}";

    return 0;

   //TODO: need a callback here to report if the the sync was successfull or not
}
/*
PDL_bool callSyncConfig(PDL_JSParameters *parms) {
    //initConfig();
    pthread_t thread1;
    int  iret1;

    char *foobar;

    iret1 = pthread_create( &thread1, NULL, syncConfig, (void *) foobar);
}

PDL_bool uninstallModule(PDL_JSParameters *parms) {
    //void uninstallModule(const char *modName) {
    init();
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
}


PDL_bool listRemoteSources(PDL_JSParameters *parms) {
    init();
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

    const std::string& tmp = sources.str();

    //const char *params[1];
    //params[0] = cstr;
    //PDL_Err mjErr = PDL_CallJS("returnRemoteSources", params, 1);
    PDL_Err mjErr = PDL_JSReply(parms, tmp.c_str());
    return PDL_TRUE;
}

void *refreshRemoteSource(void *foo) {
//void refreshRemoteSource(const char *sourceName) {
    std::stringstream out;
    init();
    InstallSourceMap::iterator source = installMgr->sources.find(remoteSource.c_str());
    if (source == installMgr->sources.end()) {
        out << "{\"returnValue\": false, \"message\": \"Couldn't find remote source: " << remoteSource << "\"}";
        finish(-3);
    }

    if (!installMgr->refreshRemoteSource(source->second))
        out << "{\"returnValue\": true, \"message\": \"Remote Source Refreshed\"}";
    else    out << "{\"returnValue\": false, \"message\": \"Error Refreshing Remote Source\"}";

    const std::string& tmp = out.str();
    const char* cstr = tmp.c_str();

    const char *params[1];
    params[0] = cstr;
    PDL_Err mjErr = PDL_CallJS("returnRefreshRemoteSource", params, 1);
}

PDL_bool callRefreshRemoteSource(PDL_JSParameters *parms) {
    const char* sourceName = PDL_GetJSParamString(parms, 0);
    pthread_t thread1;
    int  iret1;

    char *foobar;
    remoteSource = sourceName;

    iret1 = pthread_create( &thread1, NULL, refreshRemoteSource, (void *) foobar);
    return PDL_TRUE;
}


void listModules(SWMgr *otherMgr = 0, bool onlyNewAndUpdates = false) {
    init();
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
            //std::cout << status << "[" << module->Name() << "]  \t(" << version << ")  \t- " << module->Description() << "\n";
            if (it != mods.begin()) {
                out << ", ";
            }
            out << "{\"name\": \"" << module->Name() << "\", ";
            if (module->getConfigEntry("Lang")) {
                out << "\"lang\": \"" << module->getConfigEntry("Lang") << "\", ";
            }
            out << "\"datapath\": \"" << module->getConfigEntry("DataPath") << "\", ";
            out << "\"description\": \"" << module->getConfigEntry("Description") << "\"}";
        }
    }
    out << "]";

    const std::string& tmp = out.str();
    const char* cstr = tmp.c_str();

    const char *params[1];
    params[0] = cstr;
    PDL_Err mjErr = PDL_CallJS("returnListModules", params, 1);
}

PDL_bool remoteListModules(PDL_JSParameters *parms) {
//void remoteListModules(const char *sourceName, bool onlyNewAndUpdated = false) {
    bool onlyNewAndUpdated = false;
    const char* sourceName = PDL_GetJSParamString(parms, 0);

    init();
    InstallSourceMap::iterator source = installMgr->sources.find(sourceName);
    if (source == installMgr->sources.end()) {
        PDL_JSException(parms, "remoteListModules: Couldn't find remote source");
        finish(-3);
        return PDL_FALSE;
    }
    listModules(source->second->getMgr(), onlyNewAndUpdated);

    return PDL_TRUE;
}

PDL_bool getModuleDetails (PDL_JSParameters *parms) {
    //Get information about a module
    const char* moduleName = PDL_GetJSParamString(parms, 0);
    const char* sourceName = PDL_GetJSParamString(parms, 1);
    std::stringstream mod;

    init();
    InstallSourceMap::iterator source = installMgr->sources.find(sourceName);
    if (source == installMgr->sources.end()) {
        PDL_JSException(parms, "remoteListModules: Couldn't find remote source");
        finish(-3);
        return PDL_FALSE;
    }

    SWMgr* confReader = source->second->getMgr();
    SWModule *module = confReader->getModule(moduleName);
    if (!module) {
        PDL_JSException(parms, "getModuleDetails: Couldn't find Module");
        return PDL_FALSE;
    }

    mod << "{";

    mod << "\"name\": \"" << module->Name() << "\"";
    mod << ", \"datapath\": \"" << module->getConfigEntry("DataPath") << "\"";
    mod << ", \"description\": \"" << convertString(module->getConfigEntry("Description")) << "\"";
    if (module->getConfigEntry("Lang")) mod << ", \"lang\": \"" << module->getConfigEntry("Lang") << "\"";
    if (module->getConfigEntry("Versification")) mod << ", \"versification\": \"" << module->getConfigEntry("Versification") << "\"";
    if (module->getConfigEntry("About")) mod << ", \"about\": \"" << convertString(module->getConfigEntry("About")) << "\"";
    if (module->getConfigEntry("Version")) mod << ", \"version\": \"" << module->getConfigEntry("Version") << "\"";
    if (module->getConfigEntry("InstallSize")) mod << ", \"installSize\": \"" << module->getConfigEntry("InstallSize") << "\"";
    if (module->getConfigEntry("Copyright")) mod << ", \"copyright\": \"" << convertString(module->getConfigEntry("Copyright")) << "\"";
    if (module->getConfigEntry("DistributionLicense")) mod << ", \"distributionLicense\": \"" << module->getConfigEntry("DistributionLicense") << "\"";
    if (module->getConfigEntry("Category")) mod << ", \"category\": \"" << module->getConfigEntry("Category") << "\"";

    mod << "}";

    const std::string& tmp = mod.str();

    //const char *params[1];
    //params[0] = cstr;
    //PDL_Err mjErr = PDL_CallJS("returnGetDetails", params, 1);
    PDL_Err mjErr = PDL_JSReply(parms, tmp.c_str());
    return PDL_TRUE;
}


void localDirListModules(const char *dir) {
    std::cout << "Available Modules:\n\n";
    SWMgr mgr(dir);
    listModules(&mgr);
}

void *remoteInstallModule(void *foo) {
//void remoteInstallModule(const char *sourceName, const char *modName) {
    init();
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
}


void localDirInstallModule(const char *dir, const char *modName) {
    init();
    SWMgr lmgr(dir);
    SWModule *module;
    ModMap::iterator it = lmgr.Modules.find(modName);
    if (it == lmgr.Modules.end()) {
        fprintf(stderr, "Module [%s] not available at path [%s]\n", modName, dir);
        finish(-4);
    }
    module = it->second;
    int error = installMgr->installModule(mgr, dir, module->Name());
    if (error) {
        std::cout << "\nError installing module: [" << module->Name() << "] (write permissions?)\n";
    } else std::cout << "\nInstalled module: [" << module->Name() << "]\n";
} */

/*END INSTALL MANAGER STUFF */



Handle<Value> getModules(const Arguments& args) {
    HandleScope scope;

    refreshManagers();

    std::stringstream modules;
    ModMap::iterator it;

    modules << "[";

    for (it = displayLibrary->Modules.begin(); it != displayLibrary->Modules.end(); it++) {
        SWModule *module = (*it).second;
        if (it != displayLibrary->Modules.begin()) {
            modules << ", ";
        }
        modules << "{\"name\": \"" << module->Name() << "\", ";
        modules << "\"modType\":\"" << module->Type() << "\", ";
        if (module->getConfigEntry("Lang")) {
            modules << "\"lang\": \"" << module->getConfigEntry("Lang") << "\", ";
        }
        modules << "\"dataPath\":\"" << module->getConfigEntry("DataPath") << "\", ";
        modules << "\"descr\": \"" << convertString(module->Description()) << "\"}";
    }
    modules << "]";

    Local<Function> cb = Local<Function>::Cast(args[0]);
    const unsigned argc = 1;

    Local<Value> argv[argc] = { Local<Value>::New(String::New(modules.str().c_str())) };
    cb->Call(Context::GetCurrent()->Global(), argc, argv);

    return scope.Close(Undefined());
}

Handle<Value> syncRemoteSources(const Arguments& args) {
    HandleScope scope;

    std::stringstream sources;
    init();

    // be sure we have at least some config file already out there
    if (!FileMgr::existsFile(confPath.c_str())) {
        createBasicConfig(true, true);
        finish(1); // cleanup and don't exit
        init();    // re-init with InstallMgr which uses our new config
    }

    if (!installMgr->refreshRemoteSourceConfiguration())
        sources << "{\"returnValue\": true, \"message\": \"successfully sync remote source configuration.\"}";
    else
        sources << "{\"returnValue\": false, \"message\": \"Could not sync remote source configuration. Check your Internet connection!\"}";

    Local<Function> cb = Local<Function>::Cast(args[0]);
    const unsigned argc = 1;

    Local<Value> argv[argc] = { Local<Value>::New(String::New(sources.str().c_str())) };
    cb->Call(Context::GetCurrent()->Global(), argc, argv);

    return scope.Close(Undefined());
}

Handle<Value> getRemoteSources(const Arguments& args) {
    HandleScope scope;

    init();
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

    Local<Function> cb = Local<Function>::Cast(args[0]);
    const unsigned argc = 1;

    Local<Value> argv[argc] = { Local<Value>::New(String::New(sources.str().c_str())) };
    cb->Call(Context::GetCurrent()->Global(), argc, argv);

    return scope.Close(Undefined());
}

/* Handle<Value> getBooknames(const Arguments& args) {
    HandleScope scope;

    if (args.Length() < 2) {
        ThrowException(Exception::TypeError(String::New("Wrong number of arguments")));
        return scope.Close(Undefined());
    }

    if (!args[0]->IsString()) {
        ThrowException(Exception::TypeError(String::New("Wrong argument.")));
        return scope.Close(Undefined());
    }

    v8::String::Utf8Value param1(args[0]->ToString());
    std::string moduleName = std::string(*param1);

    std::stringstream bnames;
    std::string bnStr;

    SWModule *module = displayLibrary->getModule(moduleName.c_str());
    if (!module) {
        PDL_JSException(parms, "getBooknames: Couldn't find Module");
        return PDL_TRUE;  // assert we found the module
    }

    VerseKey *vkey = dynamic_cast<VerseKey *>(module->getKey());
    if (!vkey) {
        PDL_JSException(parms, "getBooknames: Couldn't find verse!");
        return PDL_TRUE;    // assert our module uses verses
    }
    VerseKey *vkey(module->getKey());
    //VerseKey &vk = *vkey;

    bnames << "[";
    for (int b = 0; b < 2; b++) {
        vk.setTestament(b+1);
        for (int i = 0; i < vk.BMAX[b]; i++) {
            vk.setBook(i+1);
            bnames << "{\"name\": \"" << convertString(vk.getBookName()) << "\", ";
            bnames << "\"abbrev\": \"" << convertString(vk.getBookAbbrev()) << "\", ";
            bnames << "\"cmax\": \"" << vk.getChapterMax() << "\"}";
            if (i+1 == vk.BMAX[b] && b == 1) {
                bnames << "]";
            } else {
                bnames << ", ";
            }
        }
    }

    Local<Function> cb = Local<Function>::Cast(args[1]);
    const unsigned argc = 1;

    Local<Value> argv[argc] = { Local<Value>::New(String::New(bnames.str().c_str())) };
    cb->Call(Context::GetCurrent()->Global(), argc, argv);

    return scope.Close(Undefined());
}*/

Handle<Value> getRemoteSources(const Arguments& args) {
    HandleScope scope;

    init();
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

    Local<Function> cb = Local<Function>::Cast(args[0]);
    const unsigned argc = 1;

    Local<Value> argv[argc] = { Local<Value>::New(String::New(sources.str().c_str())) };
    cb->Call(Context::GetCurrent()->Global(), argc, argv);

    return scope.Close(Undefined());
}

void Init(Handle<Object> exports, Handle<Object> module) {
    //getModules
    exports->Set(String::NewSymbol("getModules"),
        FunctionTemplate::New(getModules)->GetFunction());
    exports->Set(String::NewSymbol("syncRemoteSources"),
        FunctionTemplate::New(syncRemoteSources)->GetFunction());
    exports->Set(String::NewSymbol("getRemoteSources"),
        FunctionTemplate::New(getRemoteSources)->GetFunction());
    //getBooknames
    /*exports->Set(String::NewSymbol("getBooknames"),
        FunctionTemplate::New(getBooknames)->GetFunction());*/
}

NODE_MODULE(sword, Init)