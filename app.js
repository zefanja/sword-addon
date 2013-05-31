var sword = require('./build/Release/sword-addon');

/*sword.getModules(function(inModules){
    console.log(inModules);
}); */

/*sword.syncRemoteSources(function(inError, inValue){
    console.log(inError, inValue);
});*/

sword.getRemoteSources(function (inError, inSources) {
    console.log(inError, inSources);
    sword.refreshRemoteSource(JSON.parse(inSources)[1].name, function (inError) {
        console.log(inError);
        sword.getRemoteModules(JSON.parse(inSources)[1].name, function (inError, inModules) {
            console.log(inError, inModules);
        });
    });
});