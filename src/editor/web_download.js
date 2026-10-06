// web_download.js - Emscripten JavaScript library for the map editor (web build only).
//
// Browsers can't write files to the user's disk. The editor's Save writes the
// map into Emscripten's in-memory file system instead, then calls
// EditorDownloadFile() (declared in editor.c), which hands that file to the
// browser as a normal download.
//
// Functions in a JS library are linked like C functions, and they can use
// Emscripten's file system (FS) directly - nothing has to be exported.
addToLibrary({
    EditorDownloadFile__deps: ['$FS'],
    EditorDownloadFile: function (pathPtr, namePtr) {
        var path = UTF8ToString(pathPtr);
        var name = UTF8ToString(namePtr);
        var data = FS.readFile(path);
        var link = document.createElement('a');
        link.href = URL.createObjectURL(new Blob([data], { type: 'text/plain' }));
        link.download = name;
        document.body.appendChild(link);
        link.click();
        document.body.removeChild(link);
    },
});
