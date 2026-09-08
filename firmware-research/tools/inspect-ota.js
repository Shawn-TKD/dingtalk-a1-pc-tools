'use strict';

function resolve(name) {
  try { return Java.use(name); }
  catch (_) { return Java.use('defpackage.' + name); }
}

rpc.exports = {
  packageurl(version) {
    return new Promise((done, fail) => Java.perform(() => {
      try {
        if (!version) throw new Error('An exact version returned by the firmware query is required');
        const info = resolve('zy7').t();
        const Callback = Java.use('com.alibaba.android.dingtalk.common.Callback');
        const Cb = Java.registerClass({
          name: 'research.a1.FirmwarePackageCallback' + Date.now(),
          implements: [Callback],
          methods: {
            onSuccess(obj) {
              const Gson = Java.use('com.google.gson.Gson').$new();
              done(JSON.parse(String(Gson.toJson(obj))));
            },
            onException(code, reason) { done({error: String(code), reason: String(reason)}); }
          }
        });
        // Request the server-issued package URL only. No OTA manager, download
        // callback, task-start update or native StartOta call is invoked.
        resolve('g18').e(info.g(), false, version, Cb.$new());
      } catch (e) { fail(String(e.stack || e)); }
    }));
  },
  inspect() {
    return new Promise((done, fail) => Java.perform(() => {
      try {
        const Info = resolve('zy7');
        const info = Info.t();
        const did = info.g();
        const version = info.r(did);
        const Callback = Java.use('com.alibaba.android.dingtalk.common.Callback');
        done({deviceId: String(did), firmwareVersion: String(version),
          callbackMethods: Callback.class.getDeclaredMethods().map(x => String(x)),
          queryMethods: resolve('o08').class.getDeclaredMethods().map(x => String(x)),
          infoMethods: Info.class.getDeclaredMethods().map(x => String(x)).filter(x => / g\(| r\(/.test(x))});
      } catch (e) { fail(String(e.stack || e)); }
    }));
  },
  query(versionOverride) {
    return new Promise((done, fail) => Java.perform(() => {
      try {
        const info = resolve('zy7').t();
        const did = info.g();
        const version = versionOverride || String(info.r(did));
        const Callback = Java.use('com.alibaba.android.dingtalk.common.Callback');
        const Cb = Java.registerClass({
          name: 'research.a1.FirmwareQueryCallback' + Date.now(),
          implements: [Callback],
          methods: {
            onSuccess(obj) {
              const Gson = Java.use('com.google.gson.Gson').$new();
              done({currentVersion: version, result: JSON.parse(String(Gson.toJson(obj)))});
            },
            onException(code, reason) { done({error: String(code), reason: String(reason)}); }
          }
        });
        resolve('o08').b(did, version, Cb.$new());
      } catch (e) { fail(String(e.stack || e)); }
    }));
  }
};
