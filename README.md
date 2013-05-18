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

sword.syncRemoteSources(function(inError, inValue){
    //inError is 'null' on success otherwise there will be an error
    if (!inError) {
        //'inValue' is a JSON String
        //Do some stuff with 'inValue'
        console.log(inValue);
    }
});
```

Licence
-------

sword-addon is licenced under the GNU GPLv3.