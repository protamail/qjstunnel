import * as std from 'std';
import * as os from 'os';
import * as b from "test-bundle.js";

globalThis.std = std;
globalThis.os = os;

globalThis.handleRequest = function(method, path, ...searchNameValue) {
    let a = callJava("eee","q","Саша", [3, 3.1415, "heh", ["r",2.3], true], false);
    for (let s of a)
        console.log(s);
}
//throw new Error(1)

console.log("Hello from JS");

//b.a();
