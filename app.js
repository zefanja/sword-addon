var sword = require('./build/Release/sword-addon');

/*sword.getModules(function(inModules){
    console.log(inModules);
}); */

/*sword.syncRemoteSources(function(inError, inValue){
    console.log(inError, inValue);
});*/

sword.getRemoteSources(function (inError, inSources) {
    console.log(inError, inSources);
    sword.getRemoteModules({sourceName: JSON.parse(inSources)[3].name, refresh: true}, function (inError, inModules) {
        console.log(inError, JSON.parse(inModules));
    });
});
