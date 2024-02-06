# 1 billion row challenge

Based on https://github.com/gunnarmorling/1brc. For discussion see [my blog post](https://blog.jacobvosmaer.nl/0020-1-billion-row-challenge/).

To generate 1 billion rows of data:

```
make gendata
./gendata 1000000000 < data/weather_stations.csv > data/measurements.txt
```
