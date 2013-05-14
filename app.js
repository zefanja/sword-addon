var sword = require('./build/Release/sword');

/*sword.getModules(function(inModules){
    console.log(inModules);
});

sword.syncRemoteSources(function(inValue){
    console.log(inValue);
});*/

sword.getRemoteSources(function (inSources) {
    console.log(inSources);
});