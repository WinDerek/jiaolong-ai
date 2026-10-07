# Container for Building Jiaolong

## Build Container

```shell
$ https_proxy=http://127.0.0.1:8118 http_proxy=http://127.0.0.1:8118 podman build --network=host -t jiaolong-builder -f Dockerfile 
```

Here we use http proxy to bypass GFW.

## Export Container

```shell
$ podman save -o jiaolong_builder.tar 2c90924bf01f
```

## Load Container from Exported Tar File

```shell
$ podman load -i jiaolong_builder.tar
$ podman tag ${IMAGE_ID} jiaolong-builder:latest
```
