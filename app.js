var sword = require('./build/Release/sword-addon');

sword.syncRemoteSources(function(inError){
    //inError is null if there is no error.
    if(inError === null)
        //get a list of the remote Sources / Repositories
        sword.getRemoteSources(function (inError, inSources) {
            //inSources is a JSON Object with a list of all repositories
            //get a list of all modules in a remote source (as JSON)
            sword.getRemoteModules({sourceName: JSON.parse(inSources)[0].name, refresh: true}, function (inError, inModules) {
                //[{name: "foo", lang: "en", about: "...", ...}, {...}]
                console.log(inModules);
            });
        });
});

