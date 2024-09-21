import requests
import json
import base64
import os
import time
import sys

auth_encoded = os.getenv('spotifyEncodedAuth')
AUTH_HEADER = "Basic " + auth_encoded
SPOTIFY_ACCOUNTS_ENDPOINT = "https://accounts.spotify.com/api/token"
REFRESH_TOKEN = os.getenv('spotifyRefreshToken')
refreshParams = {"grant_type" : "refresh_token"}
refreshParams["refresh_token"] = REFRESH_TOKEN

refreshHeaders = {"Content-Type":"application/x-www-form-urlencoded"}
refreshHeaders["Accept"] = "application/json"
refreshHeaders["Authorization"] = AUTH_HEADER


def refreshAuthorization():
    resp = requests.post(SPOTIFY_ACCOUNTS_ENDPOINT, params=refreshParams, headers=refreshHeaders)
    token = parseToken(json.dumps(resp.json()))
    return token

def parseToken(jsonOb):
    return json.loads(jsonOb).get("access_token")

CURRENTLY_PLAYING_ENDPOINT = 'https://api.spotify.com/v1/me/player/currently-playing'

def get_song_artist(token):
    #auth_headers = {"Accept":"application/json"}
    #auth_headers["Content-Type"] = "application/json"
    #auth_headers["Authorization"] = "Bearer " + token
    auth_headers = {"Authorization":f"Bearer {token}"}

    resp = requests.get(CURRENTLY_PLAYING_ENDPOINT, headers=auth_headers)
    if(resp.status_code == 200):
        data = resp.json()
        song = data['item']['name']
        artist = data['item']['artists'][0]['name']
        return"{0}, {1}".format(song, artist)
    else:
        return "no active spotify session"


def main():
    while True:
        req = get_song_artist(refreshAuthorization())
        print("{0}\n".format(req))
        sys.stdout.flush()
        time.sleep(3)

if __name__ == "__main__":
    main()