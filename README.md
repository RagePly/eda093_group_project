# EDA093 Group Project

Group name: `Groups 3`

## Build environment

The [Dockerfile](./Dockerfile) defines the build environment.
Example on how to build `lsh` in [lab_1](./lab_1/):

```bash
docker build -t eda093:latest -f Dockerfile .                               # Build the image (once)
docker run --rm -t -v ./lab_1:/work eda093:latest cmake -S code -B build    # Configure project (once)
docker run --rm -t -v ./lab_1:/work eda093:latest cmake --build build       # Build project
docker run --rm -it -v ./lab_1:/work eda093:latest ./build/lsh              # Run lsh
```

