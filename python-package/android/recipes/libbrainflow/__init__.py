from os.path import exists, join
import sh
from pythonforandroid.recipe import NDKRecipe, PythonRecipe
from pythonforandroid.util import current_directory
from pythonforandroid.logger import shprint
from multiprocessing import cpu_count


class LibBrainflowRecipe(NDKRecipe):
    version = '3.8.1'
    url = 'https://github.com/brainflow-dev/brainflow/archive/3.8.1.tar.gz'
    sha256sum = 'f635c891d01471dad17f6b0cea3088054764eff4180318d8f2f305ddded6db84'
    md5sum = '8f951e3c0671b1bfe8bd2577721c4b42'

    def should_build(self, arch):
        return not exists(join(self.get_lib_dir(arch), 'libBoardController.so'))

    def get_lib_dir(self, arch):
        return join(self.get_build_dir(arch.arch), 'compiled')

    def build_arch(self, arch):
        build_dir = join(self.get_build_dir(arch.arch), 'build')
        shprint(sh.mkdir, '-p', build_dir)

        with current_directory(build_dir):
            env = self.get_recipe_env(arch)

            python_major = self.ctx.python_recipe.version[0]
            python_include_root = self.ctx.python_recipe.include_root(arch.arch)
            python_site_packages = self.ctx.get_site_packages_dir()
            python_link_root = self.ctx.python_recipe.link_root(arch.arch)
            python_link_version = self.ctx.python_recipe.link_version
            python_library = join(python_link_root,
                                  'libpython{}.so'.format(python_link_version))

            shprint(sh.cmake,
                    '-DANDROID_ABI={}'.format(arch.arch),
                    '-DANDROID_NATIVE_API_LEVEL={}'.format(self.ctx.ndk_api),

                    '-DCMAKE_TOOLCHAIN_FILE={}'.format(
                        join(self.ctx.ndk_dir, 'build', 'cmake',
                             'android.toolchain.cmake')),

                    self.get_build_dir(arch.arch),
                    _env=env)
            shprint(sh.make, '-j' + str(cpu_count()))

recipe = LibBrainflowRecipe()
