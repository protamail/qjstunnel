/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class org_scriptable_QJSConnector */

#ifndef _Included_org_scriptable_QJSConnector
#define _Included_org_scriptable_QJSConnector
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     org_scriptable_QJSConnector
 * Method:    newQJSRuntime
 * Signature: (Ljava/lang/String;Ljava/lang/String;)[B
 */
JNIEXPORT jbyteArray JNICALL Java_org_scriptable_QJSConnector_newQJSRuntime
  (JNIEnv *, jclass, jstring, jstring);

/*
 * Class:     org_scriptable_QJSConnector
 * Method:    freeQJSRuntime
 * Signature: ([B)V
 */
JNIEXPORT void JNICALL Java_org_scriptable_QJSConnector_freeQJSRuntime
  (JNIEnv *, jclass, jbyteArray);

/*
 * Class:     org_scriptable_QJSConnector
 * Method:    callQJS
 * Signature: ([B[Ljava/lang/Object;)I
 */
JNIEXPORT jint JNICALL Java_org_scriptable_QJSConnector_callQJS
  (JNIEnv *, jobject, jbyteArray, jobjectArray);

/*
 * Class:     org_scriptable_QJSConnector
 * Method:    exec_cmd
 * Signature: ([Ljava/lang/String;)I
 */
JNIEXPORT jint JNICALL Java_org_scriptable_QJSConnector_exec_1cmd
  (JNIEnv *, jobject, jobjectArray);

#ifdef __cplusplus
}
#endif
#endif
