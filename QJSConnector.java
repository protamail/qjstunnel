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
    native Object[] getQJSException(byte[] ctx);
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
    static final class QJSRuntime {
        protected byte[] ctx;

        private QJSRuntime(byte[] ctx) {
            this.ctx = ctx;
        }

        public static QJSRuntime getInstance(QJSConnector c) {
            QJSRuntime rt = perThread.get();
            if (rt == null) synchronized(QJSRuntime.class) {
                rt = new QJSRuntime(newQJSRuntime(c.filename, c.mainFunc));
                if (rt.ctx.length == 0)
                    throw new RuntimeException("QJSConnector: failed to create quickjs runtime!");
                perThread.set(rt);
                String compileError = c.getErrorStackTrace(rt);
                if (compileError != null) {
                    System.err.println("Error while loading " + c.filename);
                    release();
                    throw new RuntimeException(compileError);
                }
            }
            return rt;
        }

        public synchronized static void release() {
            QJSRuntime rt = perThread.get();
            if (rt != null) {
                perThread.remove();
                freeQJSRuntime(rt.ctx);
            }
        }

        @Override protected void finalize() {
            synchronized(QJSRuntime.class) {
                freeQJSRuntime(ctx);
            }
        }
    }

    public String getErrorStackTrace(QJSRuntime rt) {
        String error = null;
        Object[] st = getQJSException(rt.ctx);
        if (st != null && st.length > 0) {
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < st.length; i++) {
                sb.append(st[i] == null? "NULL" : st[i].toString());
                sb.append("\n");
            }
            error = sb.toString();
        }
        return error;
    }

    /* return null if OK, or error stack trace otherwise */
    public String callQJS(Object... argv) {
        QJSRuntime rt = QJSRuntime.getInstance(this);
        int ret = callQJS(rt.ctx, argv);
        String error = null;
        if (ret < 0) {
            error = getErrorStackTrace(rt);
            QJSRuntime.release();
        }
        return error;
    }

    public static void main(String[] args) {
        QJSConnector c = new QJSConnector("./test.js", "handleRequest");

        for (int i = 0; i < 1000000; i++) {
            String r = c.callQJS("GET", "/test", "param1", "Саша");
            if (r != null)
                System.err.print(r);
        }
        QJSRuntime.release();
    }
}

