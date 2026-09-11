#!/bin/bash

# Requires a local http server serving the files in beep/device/src/testdata
# When running this test, you may want to run boom tail <DEV_ID>
DEV_ID="$1"
if [ -z "$DEV_ID" ]; then
    echo "Usage: ./test_app_generic.sh <device_id>"
    exit 1
fi

echo '* Stop device'
boom stop $DEV_ID &> /dev/null
sleep 1

echo '* Start device'
boom start $DEV_ID &> /dev/null
sleep 5

echo '* Invalid Data Test'
for i in {1..10}
do
    echo '  (Enqueuing invalid data in 10s)'
    sleep 10
    boom ubus $DEV_ID call beep.app.webradio play_list \
        '{"items":[
            {
                "url":"http://localhost:8000/doesnotexist.mp3",
                "headers":"X-ExampleHeader: XYZ\nX-AnotherHeader: ABC\n",
                "metadata":{
                    "track_title":"Annoying Guitar Song #1",
                    "track_artist":"M. Key McKee",
                    "track_album":"Annoying Guitar Songs",
                    "album_art_url":"http://www.example.com/404.jpg",
                    "station_name":"Songs Nick is Tired Of",
                    "station_art_url":"http://www.example.com/404.jpg"},
                "type":"m"
            },
            {
                "url":"http://localhost:8000/doesnotexist.mp3",
                "headers":"In\nVal:Id:Header=!@#$%^&*()\n",
                "metadata":{
                    "bad_metadata_field":"Break Stuff",
                    "track_title":"Annoying Guitar Song #1",
                    "track_artist":"M. Key McKee",
                    "track_album":"Annoying Guitar Songs",
                    "album_art_url":"http://www.example.com/404.jpg",
                    "station_name":"Songs Nick is Tired Of",
                    "station_art_url":"http://www.example.com/404.jpg"},
                "type":"m"
            },
            {
                "url":"http://doesnotexist:8000/rcr.ogg",
                "headers":"X-ExampleHeader: XYZ\nX-AnotherHeader: ABC\n",
                "metadata":{
                    "track_title":"River City Ransom Theme",
                    "track_artist":"Billy and D. Hole",
                    "track_album":"Amazing Game Music",
                    "album_art_url":"http://www.example.com/404.jpg",
                    "station_name":"Songs Nick is Tired Of",
                    "station_art_url":"http://www.example.com/404.jpg"},
                "type":"o"
            },
            {
                "url":"http://localhost:1234/castlevania.wav",
                "headers":"X-ExampleHeader: XYZ\nX-AnotherHeader: ABC\n",
                "metadata":{
                    "track_title":"Bloody Tears",
                    "track_artist":"Vampire Hunter Dave",
                    "track_album":"Amazing Game Music",
                    "album_art_url":"http://www.example.com/404.jpg",
                    "station_name":"Songs Nick is Tired Of",
                    "station_art_url":"http://www.example.com/404.jpg"},
                "type":"p"
            },
            {
                "headers":"X-ExampleHeader: XYZ\nX-AnotherHeader: ABC\n",
                "metadata":{
                    "track_title":"Bloody Tears",
                    "track_artist":"Vampire Hunter Dave",
                    "track_album":"Amazing Game Music",
                    "album_art_url":"http://www.example.com/404.jpg",
                    "station_name":"Songs Nick is Tired Of",
                    "station_art_url":"http://www.example.com/404.jpg"},
                "type":"p"}]}' &> /dev/null
done

sleep 10

echo '* Play List Spam Test'
for i in {1..100}
do
    boom ubus $DEV_ID call beep.app.webradio play_list \
        '{"items":[
            {
                "url":"http://localhost:8000/mckee.mp3",
                "headers":"X-ExampleHeader: XYZ\nX-AnotherHeader: ABC\n",
                "metadata":{
                    "track_title":"Annoying Guitar Song #1",
                    "track_artist":"M. Key McKee",
                    "track_album":"Annoying Guitar Songs",
                    "album_art_url":"http://www.example.com/404.jpg",
                    "station_name":"Songs Nick is Tired Of",
                    "station_art_url":"http://www.example.com/404.jpg"},
                "type":"m"
            },
            {
                "url":"http://localhost:8000/rcr.ogg",
                "headers":"X-ExampleHeader: XYZ\nX-AnotherHeader: ABC\n",
                "metadata":{
                    "track_title":"River City Ransom Theme",
                    "track_artist":"Billy and D. Hole",
                    "track_album":"Amazing Game Music",
                    "album_art_url":"http://www.example.com/404.jpg",
                    "station_name":"Songs Nick is Tired Of",
                    "station_art_url":"http://www.example.com/404.jpg"},
                "type":"o"
            },
            {
                "url":"http://localhost:8000/castlevania.wav",
                "headers":"X-ExampleHeader: XYZ\nX-AnotherHeader: ABC\n",
                "metadata":{
                    "track_title":"Bloody Tears",
                    "track_artist":"Vampire Hunter Dave",
                    "track_album":"Amazing Game Music",
                    "album_art_url":"http://www.example.com/404.jpg",
                    "station_name":"Songs Nick is Tired Of",
                    "station_art_url":"http://www.example.com/404.jpg"},
                "type":"p"
            }]}' &> /dev/null
done

for i in {1..2}
do
    echo '  (Audio should be playing...Skipping to next song in 10s)'
    sleep 10
    boom ubus $DEV_ID call beep.distributor skip &> /dev/null
done

echo '  (Audio should be playing...Next test in 10s)'
sleep 10

echo '* Enqueue Test'
for i in {1..1000}
do
    boom ubus $DEV_ID call beep.app.webradio enqueue \
        '{"url":"http://localhost:8000/rcr.ogg",
          "headers":"X-ExampleHeader: XYZ\nX-AnotherHeader: ABC\n",
          "metadata":{
              "track_title":"River City Ransom Theme",
              "track_artist":"Billy and D. Hole",
              "track_album":"Amazing Game Music",
              "album_art_url":"http://www.example.com/404.jpg",
              "station_name":"Songs Nick is Tired Of",
              "station_art_url":"http://www.example.com/404.jpg"},
          "type":"o"}' &> /dev/null
done

echo '  (Audio should be playing...Bursts of skips starting soon)'
for i in {1..9}
do
    sleep 10
    echo '  (100x SKIP)'
    for j in {1..100}
    do
        boom ubus $DEV_ID call beep.distributor skip &> /dev/null
    done
done

echo '* Enqueue List Spam Test'
echo '  (Clear playlist with play)'

boom ubus $DEV_ID call beep.app.webradio play \
    '{"url":"http://localhost:8000/mckee.mp3",
      "headers":"X-ExampleHeader: XYZ\nX-AnotherHeader: ABC\n",
      "metadata":{
          "track_title":"Annoying Guitar Song #1",
          "track_artist":"M. Key McKee",
          "track_album":"Annoying Guitar Songs",
          "album_art_url":"http://www.example.com/404.jpg",
          "station_name":"Songs Nick is Tired Of",
          "station_art_url":"http://www.example.com/404.jpg"},
      "type":"m"}' &> /dev/null

echo '  (Spam queue in 1s)'
sleep 1

for i in {1..100}
do
    boom ubus $DEV_ID call beep.app.webradio enqueue_list \
        '{"items":[
            {
                "url":"http://localhost:8000/mckee.mp3",
                "headers":"X-ExampleHeader: XYZ\nX-AnotherHeader: ABC\n",
                "metadata":{
                    "track_title":"Annoying Guitar Song #1",
                    "track_artist":"M. Key McKee",
                    "track_album":"Annoying Guitar Songs",
                    "album_art_url":"http://www.example.com/404.jpg",
                    "station_name":"Songs Nick is Tired Of",
                    "station_art_url":"http://www.example.com/404.jpg"},
                "type":"m"
            },
            {
                "url":"http://localhost:8000/rcr.ogg",
                "headers":"X-ExampleHeader: XYZ\nX-AnotherHeader: ABC\n",
                "metadata":{
                    "track_title":"River City Ransom Theme",
                    "track_artist":"Billy and D. Hole",
                    "track_album":"Amazing Game Music",
                    "album_art_url":"http://www.example.com/404.jpg",
                    "station_name":"Songs Nick is Tired Of",
                    "station_art_url":"http://www.example.com/404.jpg"},
                "type":"o"
            },
            {
                "url":"http://localhost:8000/castlevania.wav",
                "headers":"X-ExampleHeader: XYZ\nX-AnotherHeader: ABC\n",
                "metadata":{
                    "track_title":"Bloody Tears",
                    "track_artist":"Vampire Hunter Dave",
                    "track_album":"Amazing Game Music",
                    "album_art_url":"http://www.example.com/404.jpg",
                    "station_name":"Songs Nick is Tired Of",
                    "station_art_url":"http://www.example.com/404.jpg"},
                "type":"p"
            }]}' &> /dev/null
done

echo '  (Audio should be playing...Skips starting soon)'
for i in {1..9}
do
    sleep 10
    echo '  (2x SKIP)'
    for j in {1..2}
    do
        boom ubus $DEV_ID call beep.distributor skip &> /dev/null
    done
done

echo '  (Next test in 5s)'
sleep 5

echo '* Parallel random requests'
for i in {1..10}
do
    echo '  (Next burst in 10s)'
    sleep 10
    for i in {1..100}
    do
        ACTION=$RANDOM
        let "ACTION %= 5"
        echo "ACTION:  $ACTION"
        case "$ACTION" in
            0)  echo '  (SKIP)'
                boom ubus $DEV_ID call beep.distributor skip
                ;;
            1)  echo '  (PLAY)'
                boom ubus $DEV_ID call beep.app.webradio play \
                    '{"url":"http://localhost:8000/mckee.mp3",
                      "headers":"X-ExampleHeader: XYZ\nX-AnotherHeader: ABC\n",
                      "metadata":{
                          "track_title":"Annoying Guitar Song #1",
                          "track_artist":"M. Key McKee",
                          "track_album":"Annoying Guitar Songs",
                          "album_art_url":"http://www.example.com/404.jpg",
                          "station_name":"Songs Nick is Tired Of",
                          "station_art_url":"http://www.example.com/404.jpg"},
                      "type":"m"}'
                ;;
            2)  echo '  (ENQUEUE)'
                boom ubus $DEV_ID call beep.app.webradio enqueue \
                    '{"url":"http://localhost:8000/rcr.ogg",
                      "headers":"X-ExampleHeader: XYZ\nX-AnotherHeader: ABC\n",
                      "metadata":{
                          "track_title":"River City Ransom Theme",
                          "track_artist":"Billy and D. Hole",
                          "track_album":"Amazing Game Music",
                          "album_art_url":"http://www.example.com/404.jpg",
                          "station_name":"Songs Nick is Tired Of",
                          "station_art_url":"http://www.example.com/404.jpg"},
                      "type":"o"}'
                ;;
            3)  echo '  (PLAY_LIST)'
                boom ubus $DEV_ID call beep.app.webradio play_list \
                    '{"items":[
                        {
                            "url":"http://localhost:8000/mckee.mp3",
                            "headers":"X-ExampleHeader: XYZ\nX-AnotherHeader: ABC\n",
                            "metadata":{
                                "track_title":"Annoying Guitar Song #1",
                                "track_artist":"M. Key McKee",
                                "track_album":"Annoying Guitar Songs",
                                "album_art_url":"http://www.example.com/404.jpg",
                                "station_name":"Songs Nick is Tired Of",
                                "station_art_url":"http://www.example.com/404.jpg"},
                            "type":"m"
                        },
                        {
                            "url":"http://localhost:8000/rcr.ogg",
                            "headers":"X-ExampleHeader: XYZ\nX-AnotherHeader: ABC\n",
                            "metadata":{
                                "track_title":"River City Ransom Theme",
                                "track_artist":"Billy and D. Hole",
                                "track_album":"Amazing Game Music",
                                "album_art_url":"http://www.example.com/404.jpg",
                                "station_name":"Songs Nick is Tired Of",
                                "station_art_url":"http://www.example.com/404.jpg"},
                            "type":"o"
                        },
                        {
                            "url":"http://localhost:8000/castlevania.wav",
                            "headers":"X-ExampleHeader: XYZ\nX-AnotherHeader: ABC\n",
                            "metadata":{
                                "track_title":"Bloody Tears",
                                "track_artist":"Vampire Hunter Dave",
                                "track_album":"Amazing Game Music",
                                "album_art_url":"http://www.example.com/404.jpg",
                                "station_name":"Songs Nick is Tired Of",
                                "station_art_url":"http://www.example.com/404.jpg"},
                            "type":"p"
                        }]}'
                ;;
            4)  echo '  (ENQUEUE_LIST)'
                boom ubus $DEV_ID call beep.app.webradio enqueue_list \
                    '{"items":[
                        {
                            "url":"http://localhost:8000/mckee.mp3",
                            "headers":"X-ExampleHeader: XYZ\nX-AnotherHeader: ABC\n",
                            "metadata":{
                                "track_title":"Annoying Guitar Song #1",
                                "track_artist":"M. Key McKee",
                                "track_album":"Annoying Guitar Songs",
                                "album_art_url":"http://www.example.com/404.jpg",
                                "station_name":"Songs Nick is Tired Of",
                                "station_art_url":"http://www.example.com/404.jpg"},
                            "type":"m"
                        },
                        {
                            "url":"http://localhost:8000/rcr.ogg",
                            "headers":"X-ExampleHeader: XYZ\nX-AnotherHeader: ABC\n",
                            "metadata":{
                                "track_title":"River City Ransom Theme",
                                "track_artist":"Billy and D. Hole",
                                "track_album":"Amazing Game Music",
                                "album_art_url":"http://www.example.com/404.jpg",
                                "station_name":"Songs Nick is Tired Of",
                                "station_art_url":"http://www.example.com/404.jpg"},
                            "type":"o"
                        },
                        {
                            "url":"http://localhost:8000/castlevania.wav",
                            "headers":"X-ExampleHeader: XYZ\nX-AnotherHeader: ABC\n",
                            "metadata":{
                                "track_title":"Bloody Tears",
                                "track_artist":"Vampire Hunter Dave",
                                "track_album":"Amazing Game Music",
                                "album_art_url":"http://www.example.com/404.jpg",
                                "station_name":"Songs Nick is Tired Of",
                                "station_art_url":"http://www.example.com/404.jpg"},
                            "type":"p"
                        }]}'
                ;;
            *)  echo '  (?????)'
                ;;
        esac
    done
done


