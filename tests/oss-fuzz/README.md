# Overview

This document provides links and knowledge about fuzzing libavif on oss-fuzz.

# Fuzzing AVIF on oss-fuzz

## Creating targets

[fuzztest](https://github.com/google/fuzztest/) is the framework of choice. Any fuzztest test declared in libavif/tests/CMakeLists.txt will be picked up and added to the list.

## Links

If you have the credentials, the main page [https://oss-fuzz.com](https://oss-fuzz.com) links to:

- the [crashes](https://oss-fuzz.com/testcases?project=libavif&open=yes) : some tests do not have a bug created. They are flaky, duplicates. Still, it is worth going over the list in case there is a flaky true positive.
- the [stats](https://oss-fuzz.com/fuzzer-stats?project=libavif&fuzzer=libFuzzer&job=libfuzzer_asan_libavif&group_by=by-fuzzer) for the different fuzzers
- the link to the fuzz introspector report: [https://oss-fuzz-introspector.storage.googleapis.com/index.html](https://oss-fuzz-introspector.storage.googleapis.com/index.html)

You can see the status of all projects:
[https://introspector.oss-fuzz.com/indexing-overview](https://introspector.oss-fuzz.com/indexing-overview)
or just the page for libavif:
[https://introspector.oss-fuzz.com/project-profile?project=libavif](https://introspector.oss-fuzz.com/project-profile?project=libavif)

## Gotchas

The [build.sh](https://github.com/AOMediaCodec/libavif/blob/a98fa4f760eacc26aa33ed396640253e29786cce/tests/oss-fuzz/build.sh#L1) file used to build the fuzzers has a few tricks:

- fuzztest is only compatible with libfuzzer so only build the tests for libfuzzer:
  - [https://github.com/AOMediaCodec/libavif/blob/a98fa4f760eacc26aa33ed396640253e29786cce/tests/oss-fuzz/build.sh\#L81](https://github.com/AOMediaCodec/libavif/blob/a98fa4f760eacc26aa33ed396640253e29786cce/tests/oss-fuzz/build.sh#L81)
  - do not forget extra flags: [https://github.com/AOMediaCodec/libavif/blob/a98fa4f760eacc26aa33ed396640253e29786cce/tests/oss-fuzz/build.sh\#L61](https://github.com/AOMediaCodec/libavif/blob/a98fa4f760eacc26aa33ed396640253e29786cce/tests/oss-fuzz/build.sh#L61)

## Testing locally

When you have your local checkout of [https://github.com/google/oss-fuzz](https://github.com/google/oss-fuzz), you can build the different fuzzers locally following instructions at [https://google.github.io/oss-fuzz/getting-started/new-project-guide/\#testing-locally](https://google.github.io/oss-fuzz/getting-started/new-project-guide/#testing-locally)

If you have a special branch you want to test on, just modify [projects/libavif/Dockerfile](https://github.com/google/oss-fuzz/blob/2e0110a1e36a4cdc18f0d91f48475a7759e7e80a/projects/libavif/Dockerfile#L22) and clone your branch, e.g.:

```
git clone --depth 1 --branch my_awesome_branch
```

Then run:

```
python3 infra/helper.py build_image libavif
python3 infra/helper.py build_fuzzers --sanitizer <address/memory/undefined> libavif
```

There are actually other “sanitizers” you should run to get the previously mentioned pages to be updated: ***coverage*** and ***introspector***.

```
python3 infra/helper.py build_fuzzers --sanitizer <coverage/introspector> libavif
```

There is a final thing to check that does not appear in the libavif CI:

```
python3 infra/helper.py check_build --sanitizer <address/memory/undefined> libavif
```

## More debugging

If check\_build times out, you might need to debug the oss-fuzz code itself, like in infra/base-images/base-runner/bad\_build\_check  
You then need to rebuild the dockers:

```
docker build -t gcr.io/oss-fuzz-base/base-runner "$@" infra/base-images/base-runner
```

If you need to get into your docker and debug your code from there:

```
python3 infra/helper.py shell libavif
```

Once in there, you can install anything you need through apt.
