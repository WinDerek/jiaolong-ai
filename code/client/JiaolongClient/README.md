# Jiaolong Client

## Build and Run

Prerequisite: an HTTP server running on `http://127.0.0.1:8118`.

### Desktop App

```shell
$ ./gradlew :desktopApp:run -Dhttps.proxyHost=127.0.0.1 -Dhttps.proxyPort=8118 -Dhttp.proxyHost=127.0.0.1 -Dhttp.proxyPort=8118 -Dhttps.protocols=TLSv1.2 --stacktrace
```

### Mobile App

Debug:

```shell
$ ./gradlew :androidApp:packageDebug -Dhttps.proxyHost=127.0.0.1 -Dhttps.proxyPort=8118 -Dhttp.proxyHost=127.0.0.1 -Dhttp.proxyPort=8118 -Dhttps.protocols=TLSv1.2 --stacktrace
$ adb install ./androidApp/build/outputs/apk/debug/androidApp-debug.apk
```

Release (TODO: Need to signing the APK):

```shell
$ ./gradlew :androidApp:packageRelease -Dhttps.proxyHost=127.0.0.1 -Dhttps.proxyPort=8118 -Dhttp.proxyHost=127.0.0.1 -Dhttp.proxyPort=8118 -Dhttps.protocols=TLSv1.2 --stacktrace
$ adb install
```

#### Android Wireless Debugging

In Android developer options, turn wireless debugging, and open Android Studio to pair it with the host.
