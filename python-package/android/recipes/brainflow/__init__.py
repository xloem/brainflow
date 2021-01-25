from os.path import join
from pythonforandroid.recipe import Recipe, PythonRecipe

class BrainflowRecipe(PythonRecipe):
    @property
    def version():
        return Recipe.get_recipe('libbrainflow', BrainflowRecipe.ctx).version

    depends = ['libbrainflow', 'numpy', 'nptyping', 'typish']

    call_hostpython_via_targetpython = True

    def get_build_dir(self, arch):
        libbrainflowrecipe = Recipe.get_recipe('libbrainflow', self.ctx)
        return join(libbrainflowrecipe.get_build_dir(arch), 'python-package')
   
recipe = BrainflowRecipe()
