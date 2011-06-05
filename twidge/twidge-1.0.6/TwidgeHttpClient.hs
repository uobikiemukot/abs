{-# LANGUAGE GeneralizedNewtypeDeriving, CPP #-}

-- Copyright (c) 2009, Diego Souza
-- All rights reserved.
-- 
-- Redistribution and use in source and binary forms, with or without
-- modification, are permitted provided that the following conditions are met:
-- 
--   * Redistributions of source code must retain the above copyright notice,
--     this list of conditions and the following disclaimer.
--   * Redistributions in binary form must reproduce the above copyright notice,
--     this list of conditions and the following disclaimer in the documentation
--     and/or other materials provided with the distribution.
--   * Neither the name of the <ORGANIZATION> nor the names of its contributors
--     may be used to endorse or promote products derived from this software
--     without specific prior written permission.
-- 
-- THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
-- ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
-- WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
-- DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
-- FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
-- DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
-- SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
-- CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
-- OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
-- OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-- | A type class that is able to perform HTTP requests.
module TwidgeHttpClient (CurlM(..)
                                     ) where

import Network.Curl
import Control.Monad.Fix
import Network.OAuth.Http.Request
import Network.OAuth.Http.Response
import qualified Network.OAuth.Http.HttpClient
import Control.Monad.Trans
import Data.Char (chr,ord)
import System.Log.Logger
import Network.URI
import qualified Data.ByteString.Lazy as B

d = debugM "TwidgeHttpClient"

-- | The libcurl backend
newtype CurlM a = CurlM { unCurlM :: IO a }
  deriving (Monad,MonadIO,MonadFix,Functor)

instance Network.OAuth.Http.HttpClient.HttpClient CurlM where
#if MIN_VERSION_hoauth(0,2,4)
  unpack = unCurlM
#else
  unlift = unCurlM
#endif

  request req = CurlM $ withCurlDo $ do c <- initialize
                                        setopts c opts
                                        d $ "Sending request: " ++ show req
                                        rsp <- perform_with_response_ c
                                        d $ "Got response: " ++ show 
                                          (respStatus rsp, respStatusLine rsp,
                                           respHeaders rsp, respBody rsp)
                                        if respStatus rsp < 200 || respStatus rsp >= 300
                                          then fail $ "Bad response: " ++ show (respStatus rsp)
                                          else return () 
                                        return $ RspHttp (respStatus rsp)
                                                         (respStatusLine rsp)
                                                         (fromList.respHeaders $ rsp)
                                                         (B.pack.map (fromIntegral.ord).respBody $ rsp)
    where httpVersion = case (version req)
                        of Http10 -> HttpVersion10
                           Http11 -> HttpVersion11
                           
          url = case method req of
            POST -> showURL (req {qString = fromList []})
            _ -> showURL req
          curlPostData = case method req of
            POST -> [CurlPostFields (map postopt . toList . qString $ req)]
            _ -> []
          postopt (k, v) = escapeURIString isUnreserved k ++ "=" ++
                           escapeURIString isUnreserved v
          
          curlMethod = case (method req)
                       of GET   -> [CurlHttpGet True]
                          POST  -> [CurlPost True]
                          PUT   -> [CurlPut True]
                          HEAD  -> [CurlNoBody True,CurlCustomRequest "HEAD"]
                          other -> if (B.null.reqPayload $ req)
                                   then [CurlHttpGet True,CurlCustomRequest (show other)]
                                   else [CurlPost True,CurlCustomRequest (show other)]
          curlHeaders = let headers = (map (\(k,v) -> k++": "++v).toList.reqHeaders $ req)
                        in [CurlHttpHeaders $"Expect: " 
                                            :("Content-Length: " ++ (show.B.length.reqPayload $ req))
                                            :headers
                           ]

          opts = [CurlURL (showURL req)
                 ,CurlHttpVersion httpVersion
                 ,CurlFollowLocation True  -- follow redirects
                 ,CurlFailOnError True     -- fail on server errors
                 ,CurlLowSpeedTime 60
                 ,CurlLowSpeed 1
                 ,CurlUserAgent "twidge v1.0.0; Haskell; GHC"
                 ,CurlHeader False
                 ] ++ curlHeaders
                   ++ curlMethod 
                   ++ curlPostData
          
-- vim:sts=2:sw=2:ts=2:et
