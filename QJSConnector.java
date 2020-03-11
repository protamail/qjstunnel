package org.scriptable;

public class QJSConnector {
    static {
        System.loadLibrary("quickjsc");
    }

    public native static synchronized byte[] newQJSRuntime(String filename, String mainFunc);
    public native static void freeQJSRuntime(byte[] ctx);
    public native int callQJS(byte[] ctx, String... searchNameValue);
    public native int exec_cmd(String[] cmd);

    public Object[] callJava(Object[] argv) {
/*        if (argv != null) {
            System.out.println("len="+argv.length);
            for (Object s: argv)
                System.out.println(" ->  " + s.toString());
        }*/
        Object [] ret = new Object[] {
//            new Object[] { "ldlkjflskdjfljdsk",
                                                        "dfs",
//                                                }
        };
        return ret;
//        return argv;
    }

    public static void main(String[] args) {
        QJSConnector t = new QJSConnector();

        byte[] qjsCtx = QJSConnector.newQJSRuntime("./test.js", "handleRequest");
        if (qjsCtx.length == 0)
            throw new RuntimeException("Failed to initialize QJS runtime");
//            for (int i = 0; i < 1000000; i++)
            t.callQJS(qjsCtx, "GET", "/test", "param1", "Саша");
        QJSConnector.freeQJSRuntime(qjsCtx);
    }   
}

