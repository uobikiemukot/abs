module Paths_twidge (
    version,
    getBinDir, getLibDir, getDataDir, getLibexecDir,
    getDataFileName
  ) where

import Data.Version (Version(..))
import System.Environment (getEnv)

version :: Version
version = Version {versionBranch = [1,0,6], versionTags = []}

bindir, libdir, datadir, libexecdir :: FilePath

bindir     = "/home/nak/haru/.cabal/bin"
libdir     = "/home/nak/haru/.cabal/lib/twidge-1.0.6/ghc-6.12.3"
datadir    = "/home/nak/haru/.cabal/share/twidge-1.0.6"
libexecdir = "/home/nak/haru/.cabal/libexec"

getBinDir, getLibDir, getDataDir, getLibexecDir :: IO FilePath
getBinDir = catch (getEnv "twidge_bindir") (\_ -> return bindir)
getLibDir = catch (getEnv "twidge_libdir") (\_ -> return libdir)
getDataDir = catch (getEnv "twidge_datadir") (\_ -> return datadir)
getLibexecDir = catch (getEnv "twidge_libexecdir") (\_ -> return libexecdir)

getDataFileName :: FilePath -> IO FilePath
getDataFileName name = do
  dir <- getDataDir
  return (dir ++ "/" ++ name)
