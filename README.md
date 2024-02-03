# 1 billion row challenge

Based on https://github.com/gunnarmorling/1brc.

To generate 1 billion rows of data:

```
make gendata
./gendata 1000000000 < data/weather_stations.csv > data/measurements.txt
```
