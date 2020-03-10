public abstract class QJSTunnel {
    static {
        System.loadLibrary("qjs");
        System.loadLibrary("qjstun");
    }

    public abstract Object[] callJava(Object[] argv);
    public native byte[] newQJSRuntime(String filename, String mainFunc);
    public native int callQJS(byte[] ctx, String... searchNameValue);
    public native void freeQJSRuntime(byte[] ctx);
    public native int exec_cmd(String[] cmd);
}

