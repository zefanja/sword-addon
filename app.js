
var path = require("path");
var restify = require('restify');

function getSwordPath() {
    var home = process.env.HOME || process.env.HOMEPATH || process.env.USERPROFILE;
    return path.join(home, ".sword");
}
process.env["SWORD_PATH"] = getSwordPath();

var sword = require("./build/Release/sword-addon");

//Restify Server
var server = restify.createServer();
server.pre(restify.pre.userAgentConnection());
server.use(restify.bodyParser());
//set the encoding
server.use(function(req, res, next) {
    res.setHeader("content-type", "application/json;charset=utf-8");
    next();
});

//** REST API functions **//

//get a list of all installed modules (on the server)
function getModules(req, res, next) {
    sword.getModules(function (inError, inModules) {
        if (inError)
            return next(inError);

        res.send(JSON.parse(inModules));
        return next();
    });
}

//get the books, chapters and verses in each chapter from a module
function getModuleBCV(req, res, next) {
    sword.getModuleBCV(req.params.moduleName, function (inError, inBCV) {
        if (inError)
            return next(inError);

        res.send(JSON.parse(inBCV));
        return next();
    });
}

server.get('/modules/', getModules);
server.get('/modules/:moduleName/bcv', getModuleBCV);

server.listen(1234, "127.0.0.1", function() {
    console.log('%s listening at %s', server.name, server.url);
});

/*sword.syncRemoteSources(function(inError){
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
});*/

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
/*sword.getRawText({key: "gen 1:6", moduleName: "GerNeUe"}, function (inError, inVerses) {
    console.log("%s, inVerse: ", inError, JSON.parse(inVerses)[0]);
});*/

