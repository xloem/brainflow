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

    built_libraries = {
        'libBoardController.so' : 'compiled',
        'libDataHandler.so' : 'compiled',
        'libGanglionLib.so' : 'compiled',
        'libMLModule.so' : 'compiled',
    }

    def build_arch(self, arch):
        env = self.get_recipe_env(arch)

        shprint(sh.cmake,
                '-DANDROID_ABI={}'.format(arch.arch),
                '-DANDROID_NATIVE_API_LEVEL={}'.format(self.ctx.ndk_api),

                '-DCMAKE_TOOLCHAIN_FILE={}'.format(
                    join(self.ctx.ndk_dir, 'build', 'cmake',
                         'android.toolchain.cmake')),

                self.get_build_dir(arch.arch),
                _env=env)

        shprint(sh.make, '-j' + str(cpu_count()))

    def install_libraries(self, arch):
        # place libraries in python package folder
        libs = self.get_libraries(arch)
        libs_dir = join(
            self.get_build_dir(arch.arch),
            'python-package', 'brainflow', 'lib')
        shprint(sh.mkdir, '-p', libs_dir)
        shprint(sh.cp, *libs, libs_dir)

recipe = LibBrainflowRecipe()
