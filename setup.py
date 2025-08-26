from setuptools import setup, Extension
import os

# Read the README file for long description
def read_readme():
    here = os.path.abspath(os.path.dirname(__file__))
    with open(os.path.join(here, 'README.md'), encoding='utf-8') as f:
        return f.read()

ministring_extension = Extension(
    'ministring',
    sources=['src/ministring.c'],
    include_dirs=['src'],
    language='c',
    # Add compiler flags for better performance and compatibility
    extra_compile_args=['-O3'] if os.name != 'nt' else [],  # Unix optimization
    extra_link_args=[] if os.name != 'nt' else [],  # Windows linking
)

setup(
    ext_modules=[ministring_extension],
    long_description=read_readme(),
    long_description_content_type='text/markdown',
    zip_safe=False,
)
