package org.scriptable;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.function.Predicate;
import java.lang.ref.WeakReference;

public class QuickJSConnector {
    // NOTE: since this class must be loaded from tomcat's lib using its root class loader,
    // static variables here will be shared by all apps
    String filename;
    String mainFunc;
    String ctxKey;
    private ArrayList<WeakReference<QJSRuntime>> allInstances;
    // need to maintain per app/script list, since static is shared between apps
    private static HashMap<String, ArrayList<WeakReference<QJSRuntime>>> allInstancesMap = new HashMap<>();
    long timestamp;
    static {
        System.loadLibrary("quickjsc");
    }

    private native static byte[] nativeNewQJSRuntime(String filename, String mainFunc);
    private native static void nativeFreeQJSRuntime(byte[] ctx);
    private native int nativeCallQJS(byte[] ctx, Object[] argv);
    private native Object[] nativeGetQJSException(byte[] ctx);

    public QuickJSConnector(String filename, String mainFunc, long timestamp) {
        this.filename = filename;
        this.mainFunc = mainFunc;
        this.ctxKey = makeCtxKey(filename, mainFunc);
        this.timestamp = timestamp;
        synchronized(QuickJSConnector.class) {
            if (!allInstancesMap.containsKey(this.ctxKey))
                allInstancesMap.put(this.ctxKey, new ArrayList<WeakReference<QJSRuntime>>());
            this.allInstances = allInstancesMap.get(this.ctxKey);
        }
    }

    public static String makeCtxKey(String filename, String mainFunc) {
        return filename + "/" + mainFunc;
    }

    public Object[] callJava(Object[] argv) {
        Object [] ret = new Object[] { "__error__", "sample error" };
        return ret;
    }

    // QJS runtime must be used by the thread which created it
    // Also, worker threads in tomcat are shared by all apps
    static ThreadLocal<HashMap<String, QJSRuntime>> perThread = new ThreadLocal<>();
    private static final class QJSRuntime {
        byte[] ctx;
        long timestamp;
        String ctxKey;

        @SuppressWarnings("unchecked")
        private QJSRuntime(byte[] ctx, String ctxKey, long timestamp) {
            this.ctx = ctx;
            this.ctxKey = ctxKey;
            this.timestamp = timestamp;
        }

        static QJSRuntime getInstance(QuickJSConnector c) {
            HashMap<String, QJSRuntime> rtMap = perThread.get();
            if (rtMap == null) {
                rtMap = new HashMap<>();
                perThread.set(rtMap);
            }
            QJSRuntime rt = rtMap.get(c.ctxKey);
            if (rt != null && (rt.timestamp < c.timestamp)) {
                rt.release(c.allInstances);
                rt = null;
            }
            if (rt == null || rt.ctx == null || rt.ctx.length == 0) synchronized(QuickJSConnector.class) {
                rt = new QJSRuntime(nativeNewQJSRuntime(c.filename, c.mainFunc), c.ctxKey, c.timestamp);
                c.allInstances.add(new WeakReference(rt));
                if (rt.ctx == null || rt.ctx.length == 0) {
                    rt.ctx = null;
                    throw new RuntimeException("Failed to create quickjs runtime!");
                }
                rtMap.put(c.ctxKey, rt);
                String compileError = c.getErrorStackTrace(rt);
                if (compileError != null) {
                    rt.release(c.allInstances);
                    throw new RuntimeException("Error while loading " + c.filename + "\n" + compileError);
                }
            }
            return rt;
        }

        void release(ArrayList<WeakReference<QJSRuntime>> allInstances) {
            if (ctx != null && ctx.length > 0) synchronized(QuickJSConnector.class) {
                HashMap<String, QJSRuntime> rtMap = perThread.get();
                if (rtMap != null)
                    rtMap.remove(ctxKey);
                nativeFreeQJSRuntime(ctx);
                ctx = null;
                QJSRuntime rt = this;
                allInstances.removeIf(new Predicate<WeakReference<QJSRuntime>>() {
                    @Override public boolean test(WeakReference<QJSRuntime> wr) {
                        return wr.get() == rt;
                    }
                });
            }
        }

        static void releaseAll(ArrayList<WeakReference<QJSRuntime>> allInstances) {
            synchronized(QuickJSConnector.class) {
                for (WeakReference<QJSRuntime> wr: allInstances) {
                    QJSRuntime rt = wr.get();
                    if (rt != null && rt.ctx != null && rt.ctx.length > 0) {
                        nativeFreeQJSRuntime(rt.ctx);
                        rt.ctx = null;
                    }
                    else
                        System.out.println("releaseAll: null ctx still in the allInstances array");
                }
                allInstances.clear();
            }
        }

        @Override protected void finalize() { // normally these should be released by release/releaseAll
            synchronized(QuickJSConnector.class) {
                if (ctx != null && ctx.length > 0) {
                    nativeFreeQJSRuntime(ctx);
                    ctx = null;
                    QJSRuntime rt = this;
                    ArrayList<WeakReference<QJSRuntime>> allInstances = allInstancesMap.get(ctxKey);
                    if (allInstances != null) {
                        allInstances.removeIf(new Predicate<WeakReference<QJSRuntime>>() {
                            @Override public boolean test(WeakReference<QJSRuntime> wr) {
                                return wr.get() == rt;
                            }
                        });
                    }
                }
            }
        }
    }

    public String getErrorStackTrace(QJSRuntime rt) {
        String error = null;
        Object[] st = nativeGetQJSException(rt.ctx);
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
    public int callQJS(Object[] argv) throws Exception {
        String error = null;
        int ret = 0;
        try {
            QJSRuntime rt = QJSRuntime.getInstance(this);
            ret = nativeCallQJS(rt.ctx, argv);
            if (ret < 0) {
                error = getErrorStackTrace(rt);
                rt.release(allInstances);
            }
        } catch(Exception e) {
            error = e.getMessage();
        }
        if (error != null)
            throw new Exception(error);
        return ret;
    }

    public void releaseAllRuntimes() {
        releaseAllRuntimes(filename, mainFunc);
    }

    public static void releaseAllRuntimes(String filename, String mainFunc) {
        synchronized(QuickJSConnector.class) {
            ArrayList<WeakReference<QJSRuntime>> allInstances = allInstancesMap.get(makeCtxKey(filename, mainFunc));
            if (allInstances != null)
                QJSRuntime.releaseAll(allInstances);
        }
    }

    public static void main(String[] args) {
        QuickJSConnector c = new QuickJSConnector("./test.js", "handleRequest", 0);

        for (int i = 0; i < 1000000; i++) {
            try {
                c.callQJS(new Object[] { "GET", "/test", "param1", "Саша" });
            } catch(Exception e) {
                System.err.print(e.getMessage());
            }
        }
        c.releaseAllRuntimes();
    }
}

