var sword = require('./build/Release/sword-addon');

/*sword.getModules(function(inModules){
    console.log(inModules);
}); */

/* sword.syncRemoteSources(function(inError, inValue){
    console.log(inError, inValue);
}); */

sword.getRemoteSources(function (inError, inSources) {
    console.log(inError, inSources);
});