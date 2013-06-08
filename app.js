var path = require("path");

function getSwordPath() {
    var home = process.env.HOME || process.env.HOMEPATH || process.env.USERPROFILE;
    return path.join(home, ".sword");
}
process.env["SWORD_PATH"] = getSwordPath();

var sword = require("./build/Release/sword-addon");

sword.syncRemoteSources(function(inError){
    //inError is null if there is no error.
    if(inError === null)
        //get a list of the remote Sources / Repositories
        sword.getRemoteSources(function (inError, inSources) {
            //inSources is a JSON String with a list of all repositories
            //get a list of all modules in a remote source (as JSON)
            sword.getRemoteModules({sourceName: JSON.parse(inSources)[0].name, refresh: true}, function (inError, inModules) {
                //[{name: "foo", lang: "en", about: "...", ...}, {...}]
                console.log(inModules);
            });
        });
});

//get local installed modules
/*sword.getModules(function (inError, inModules) {
    console.log(JSON.parse(inModules));
});*/

//Install a module
/*sword.getRemoteModules({sourceName: "Bible.org", refresh: true}, function (inError, inModules) {
    console.log(JSON.parse(inModules));
    sword.installModule({moduleName: "NETnotesfree", sourceName: "Bible.org"}, function (inError) {console.log("installModule", inError);});
}); */

//Get the text entry (HTML) at specific key
/*sword.getRawText({key: "mt 5", moduleName: "KJV"}, function (inError, inVerses) {
    console.log(inError, JSON.parse(inVerses));
});*/

