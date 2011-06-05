{-# LANGUAGE CPP #-}
{-
Copyright (C) 2010 John Goerzen <jgoerzen@complete.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
-}

module Commands.Setup(setup) where
import Utils
import System.Log.Logger
import Data.List
import Data.ConfigFile
import System.IO
import Data.Either.Utils
import Data.Char
import Config
import Control.Monad(when)
import Network.OAuth.Consumer
import Data.Maybe
import Network.OAuth.Http.Request
import Network.OAuth.Http.HttpClient
import OAuth
import Data.Binary(encode)
import Control.Monad.Trans
import qualified Control.Monad.State.Class as M

i = infoM "setup"
d = debugM "setup"

--------------------------------------------------
-- setup
--------------------------------------------------

setup = simpleCmd "setup" "Interactively configure twidge for first-time use"
        setup_help
        [] setup_worker

setup_worker cpath cp _ =
  do hSetBuffering stdout NoBuffering
     when (has_option cp "DEFAULT" "oauthdata")
       confirmSetup
     putStrLn "\nWelcome to twidge.  We will now configure twidge for your"
     putStrLn "use with Twitter (or a similar service).  This will be quick and easy!\n"
     putStrLn "\nPlease wait a moment while I query the server...\n\n"
       
     app <- case getApp cp of
       Nothing -> fail $ "Error: must specify oauthconsumerkey and oauthconsumersecret for non-default host " ++ (serverHost cp)
       Just x -> return x
     
     let reqUrlBase = forceEither $ get cp "DEFAULT" "oauthrequesttoken"
     let accUrlBase = forceEither $ get cp "DEFAULT" "oauthaccesstoken"
     let authUrlBase = forceEither $ get cp "DEFAULT" "oauthauthorize"
     let reqUrl = fromJust . parseURL $ reqUrlBase
     let accUrl = fromJust . parseURL $ accUrlBase
     let authUrl = ((authUrlBase ++ "?oauth_token=") ++ ) . 
                   findWithDefault ("oauth_token", "") .
                   oauthParams
     
     let CurlM resp = 
           runOAuth $ 
           do ignite app
              liftIO $ d "ignite done.  trying request 1."
              reqres1 <- tryRequest reqUrl
              case reqres1 of
#if MIN_VERSION_hoauth(0,2,4)
                AccessToken _ _ -> return ()
                _ -> -- hack around hoauth bug for identica
#else
                Left x -> -- hack around hoauth bug for identica
#endif
                  do liftIO $ d "request 1 failed.  attempting workaround."
                     putToken $ AccessToken {application = app,
                                             oauthParams = empty}
                     reqres2 <- tryRequest reqUrl
                     case reqres2 of 
#if MIN_VERSION_hoauth(0,2,4)
                       AccessToken _ _ -> return ()
                       _ -> fail $ "Error from oauthRequest."
#else
                       Left x -> fail $ "Error from oauthRequest: " ++ show x
                       Right _ -> return ()
                Right _ -> return ()
#endif
              twidgeAskAuthorization authUrl
              oauthRequest HMACSHA1 Nothing accUrl
              tok <- getToken
              return (twoLegged tok, threeLegged tok, tok)
     (leg2, leg3, response) <- resp
     -- on successful auth, leg3 is True. Otherwise, it is False.
     -- leg1 is always false and r appears to not matter.
     d $ show (leg2, leg3, oauthParams response)
     if leg3 
       then do let newcp = forceEither $ set cp "DEFAULT" "oauthdata" .
                           esc . show . fixIdentica . toList . oauthParams $ response
               writeCP cpath newcp
               putStrLn $ "Successfully authenticated!" 
               putStrLn "Twidge has now been configured for you and is ready to use."
       else putStrLn "Authentication failed; please try again"
    where confirmSetup =
              do putStrLn "\nIt looks like you have already authenticated twidge."
                 putStrLn "If we continue, I may remove your existing"
                 putStrLn "authentication.  Would you like to proceed?"
                 putStr   "\nYES or NO: "
                 c <- getLine
                 if (map toLower c) == "yes"
                    then return ()
                    else permFail "Aborting setup at user request."
          tryRequest reqUrl = 
            do reqres <- oauthRequest HMACSHA1 Nothing reqUrl
#if MIN_VERSION_hoauth(0,2,4)
               liftIO $ d $ "reqres params: " ++ (show (oauthParams reqres))
#else
               liftIO $ d $ "reqres params: " ++ case reqres of
                 Left x -> " error " ++ x
                 Right y -> show (oauthParams y)
#endif
               return reqres
          -- Work around a hoauth bug - identica doesn't return
          -- oauth_callback_confirmed
          fixIdentica :: [(String, String)] -> [(String, String)]
          fixIdentica inp =
            case lookup "oauth_callback_confirmed" inp of
              Nothing -> ("oauth_callback_confirmed", "true") : inp
              Just _ -> inp
          esc x = concatMap fix x
          fix '%' = "%%"
          fix x = [x]

twidgeAskAuthorization :: MonadIO m => (Token -> String) -> OAuthMonad m ()
twidgeAskAuthorization getUrl = 
  do token <- M.get
     answer <- liftIO $ do putStrLn "OK, next I need you to authorize Twidge to access your account."
                           putStrLn "Please cut and paste this URL and open it in a web browser:\n"
                           putStrLn (getUrl token)
                           putStrLn "\nClick Allow when prompted.  You will be given a numeric"
                           putStrLn "key in your browser window.  Copy and paste it here."
                           putStrLn "(NOTE: some non-Twitter services supply no key; just leave this blank"
                           putStrLn "if you don't get one.)\n"
                           putStr   "Authorization key: "
                           getLine
     M.put (injectOAuthVerifier answer token)

setup_help =
  "Usage: twidge setup\n\n"
