public class QJSTest extends QJSTunnel {
    byte[] qjsCtx;

    @Override
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
        QJSTest t = new QJSTest();

//        for (;;) {
            t.qjsCtx = t.newQJSRuntime("./test.js", "handleRequest");
            if (t.qjsCtx.length == 0)
                throw new RuntimeException("Failed to initialize QJS runtime");
            for (int i = 0; i < 1000000; i++)
//            for (;;)
                t.callQJS(t.qjsCtx, "GET", "/test", "param1", "Саша");
            t.freeQJSRuntime(t.qjsCtx);
//        }
    }   
}

