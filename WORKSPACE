workspace(name = "memory_alloc_with_handles")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

http_archive(
    name = "googletest",
    urls = [
        "https://github.com/google/googletest/archive/refs/tags/v1.16.0.tar.gz",
    ],
    strip_prefix = "googletest-1.16.0",
)

git_repository(
    name = "hedron_compile_commands",
    remote = "https://github.com/hedronvision/bazel-compile-commands-extractor.git",
    commit = "0e990032f3c5a866e72615cf67e5ce22186dcb97",
)

git_repository(
    name = "read_write_locks",
    remote = "https://github.com/Rochan-A/pthread-read-write-locks.git",
    branch = "master"
)
