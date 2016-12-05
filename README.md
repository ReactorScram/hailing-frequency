The server runs on port 8080

On the uploader do

    curl -T "hiddenempire.ogg" -Lv http://myserver.com:8080/ 2>&1

The uploader will get an HTTP 307 redirect  
The location of the redirect is the same place to download from  
Put that location into the download as such

    curl http://myserver.com:8080/relays/0/hiddenempire.ogg > 
    hiddenempire.ogg
