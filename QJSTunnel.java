public class QJSTunnel {
    static {
        System.loadLibrary("test");
    }

    byte[] qjsCtx;
    public Object[] callJava(Object[] argv) {
/*        if (argv != null) {
            System.out.println("len="+argv.length);
            for (Object s: argv)
                System.out.println(" ->  " + s.toString());
        }*/
        Object [] ret = new Object[] { new Object[] { "ldlkjflskdjfljdsk", new Object[] { "ldlkjflskdjfljdsk", 3 } } };
        return ret;
//        return argv;
    }

    public native int main(int argc, char[] argv);
    public native int exec_cmd(String[] cmd);
    public native byte[] newQJSRuntime(String filename, String mainFunc);
    public native void freeQJSRuntime(byte[] ctx);
    public native int callQJS(byte[] ctx, String... searchNameValue);

    public static void main(String[] args) {
        QJSTunnel t = new QJSTunnel();

//        for (;;) {
            t.qjsCtx = t.newQJSRuntime("./test.js", "handleRequest");
            if (t.qjsCtx.length == 0)
                throw new RuntimeException("Failed to initialize QJS runtime");
//            for (int i = 0; i < 1000000; i++)
//            for (;;)
                t.callQJS(t.qjsCtx, "GET", "/test", "param1", "Саша");
            t.freeQJSRuntime(t.qjsCtx);
//        }
    }   
}

