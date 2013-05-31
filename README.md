sword-addon
===========

This is a small and simple node-addon wrapper for `libsword` from [Crosswire](http://crosswire.org/sword).

This is a pre-alpha version so API will likely break in the future.

Build
-----

You need to install node-gyp first:

```
npm install -g node-gyp
```

Be sure to install `libsword` as well. After that you can build the sword-addon:

```
node-gyp configure build
```

Usage
-----

See `app.js` for how to use the current API.

Every API call takes at least one argument, the callback.

```javascript
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
```

Licence
-------

sword-addon is licenced under the GNU GPLv3.