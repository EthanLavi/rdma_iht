workspace(name = "rdma_iht")

local_repository(
    name = "rome",
    path = "../rome",
)

load("@rome//:dependencies.bzl", "rome_dependencies")

rome_dependencies()

load("@rome//:setup.bzl", "rome_setup")

rome_setup()