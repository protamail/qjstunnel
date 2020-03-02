public class QJSTunnel {
    static {
        System.loadLibrary("test");
    }

    public native int main(int argc, char[] argv);
    public native int exec_cmd(String[] cmd);
    public native void sayHello();
    public native byte[] newQJSRuntime(String filename);
    public native void freeQJSRuntime(byte[] opaque);

    public static void main(String[] args) {
        QJSTunnel t = new QJSTunnel();

        for (;;) {
            byte[] qjs = t.newQJSRuntime("./test.js");
            t.freeQJSRuntime(qjs);
        }
    }   
}

