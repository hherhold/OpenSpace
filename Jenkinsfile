import groovy.io.FileType

library('sharedSpace'); // jenkins-pipeline-lib


def url = 'https://github.com/OpenSpace/OpenSpace';
def branch = env.BRANCH_NAME;

@NonCPS
def readDir() {
  def dirsl = [];
  new File("${workspace}").eachDir() {
    dirs -> println dirs.getName() 
    if (!dirs.getName().startsWith('.')) {
      dirsl.add(dirs.getName());
    }
  }
  return dirs;
}

def moduleCMakeFlags() {
  def modules = [];
  // using new File doesn't work as it is not allowed in the sandbox
  
  if (isUnix()) {
     modules = sh(returnStdout: true, script: 'ls -d modules/*').trim().split('\n');
  };
  else {
    modules = bat(returnStdout: true, script: '@dir modules /b /ad /on').trim().split('\r\n');
  }

  // def dirs = readDir();
  // def currentDir = new File('.')
  // def dirs = []
  // currentDir.eachFile FileType.DIRECTORIES, {
  //     dirs << it.name
  // }
  // def moduleFlags = [
  //   'atmosphere',
  //   'base',
  //   // 'cefwebgui',
  //   'debugging',
  //   'digitaluniverse',
  //   'fieldlines',
  //   'fieldlinessequence',
  //   'fitsfilereader',
  //   'gaia',
  //   'galaxy',
  //   'globebrowsing',
  //   'imgui',
  //   'iswa',
  //   'kameleon',
  //   'kameleonvolume',
  //   'multiresvolume',
  //   'server',
  //   'space',
  //   'spacecraftinstruments',
  //   'space',
  //   'spout',
  //   'sync',
  //   'touch',
  //   'toyvolume',
  //   'volume',
  //   // 'webbrowser',
  //   // 'webgui'
  // ];

  def flags = '';
  for (module in modules) {
      flags += "-DOPENSPACE_MODULE_${module.toUpperCase()}=ON "
  }
  return flags;
}

// echo flags

//
// Pipeline start
//

parallel tools: {
  node('tools') {
    stage('tools/scm') {
      deleteDir();
      gitHelper.checkoutGit(url, branch, false);
      helper.createDirectory('build');
    }
    stage('tools/cppcheck') {
      sh(
        script: 'cppcheck --enable=all --xml --xml-version=2 -i ext --suppressions-list=support/cppcheck/suppressions.txt include modules src tests 2> build/cppcheck.xml',
        label: 'CPPCheck'
      )
      recordIssues(
        id: 'tools-cppcheck',
        tool: cppCheck(pattern: 'build/cppcheck.xml')
      ) 
    }  
    cleanWs()
  } // node('tools')
},
linux_gcc_make: {
  if (env.USE_BUILD_OS_LINUX == 'true') {
    node('linux' && 'gcc') {
      stage('linux-gcc-make/scm') {
        deleteDir();
        gitHelper.checkoutGit(url, branch);
      }
      //stage('linux-gcc-make/build') {
      //    def cmakeCompileOptions = moduleCMakeFlags();
      //    cmakeCompileOptions += ' -DMAKE_BUILD_TYPE=Release';
      //    // Not sure why the linking of OpenSpaceTest takes so long
      //    compileHelper.build(compileHelper.Make(), compileHelper.Gcc(), cmakeCompileOptions, 'OpenSpace', 'build-make');
      //    compileHelper.recordCompileIssues(compileHelper.Gcc());
      //}
      stage('linux-gcc-make/img-compare') {
        sh 'if [ -l ${IMAGE_TESTING_BASE_PATH}/jenkinsRecentBuild ]; then rm ${IMAGE_TESTING_BASE_PATH}/jenkinsRecentBuild; fi'
        sh 'ln -s $(pwd) ${IMAGE_TESTING_BASE_PATH}/jenkinsRecentBuild'
        sh 'echo jenkinsRecentBuild > ${IMAGE_TESTING_BASE_PATH}/latestBuild.txt'
      }
      stage('linux-gcc-make/test') {
        // testHelper.runUnitTests('build/OpenSpaceTest');
        // testHelper.runUnitTests('bin/codegentest')
      }
      cleanWs()
    } // node('linux')
  }
}
