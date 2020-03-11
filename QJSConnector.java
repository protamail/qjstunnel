package org.scriptable;

import java.util.ArrayList;

public class QJSConnector {
    String filename;
    String mainFunc;
    static {
        System.loadLibrary("quickjsc");
    }

    native static byte[] newQJSRuntime(String filename, String mainFunc);
    native static void freeQJSRuntime(byte[] ctx);
    native int callQJS(byte[] ctx, Object[] argv);
    native int exec_cmd(String[] cmd);

    public QJSConnector(String filename, String mainFunc) {
        this.filename = filename;
        this.mainFunc = mainFunc;
    }

    public Object[] callJava(Object[] argv) {
        Object [] ret = new Object[] { "dfs" };
        return ret;
    }

    // QJS runtime must be used by the thread which created it
    static ThreadLocal<QJSRuntime> perThread = new ThreadLocal<QJSRuntime>();
    public static final class QJSRuntime {
        protected byte[] ctx;

        private QJSRuntime(byte[] ctx) {
            this.ctx = ctx;
        }

        public static QJSRuntime getInstance(String filename, String mainFunc) {
            QJSRuntime ret = perThread.get();
            if (ret == null) synchronized(QJSRuntime.class) {
                ret = new QJSRuntime(newQJSRuntime(filename, mainFunc));
                if (ret.ctx.length == 0)
                    throw new RuntimeException("QJSRuntime: failed to create QJS instance!");
                perThread.set(ret);
            }
            return ret;
        }

        public synchronized static void release() {
            QJSRuntime ret = perThread.get();
            if (ret != null) {
                perThread.remove();
                freeQJSRuntime(ret.ctx);
            }
        }

        @Override protected void finalize() {
            synchronized(QJSRuntime.class) {
                freeQJSRuntime(ctx);
            }
        }
    }

    public int callQJS(Object... argv) {
        QJSRuntime rt = QJSRuntime.getInstance(filename, mainFunc);
        int ret = callQJS(rt.ctx, argv);
        if (ret == -1 /* some sort of critical error */)
            QJSRuntime.release();
        return ret;
    }

    public static void main(String[] args) {
        QJSConnector c = new QJSConnector("./test.js", "handleRequest");

    for (int i = 0; i < 5; i++)
        c.callQJS("GET", "/test", "param1", "Саша");
        QJSRuntime.release();
    }
}

