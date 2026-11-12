from setuptools import Extension, setup

setup(
    ext_modules=[
        Extension("specific_json._core", ["specific_json/_core.c"])
    ]
)