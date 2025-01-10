import json
import csv
import time
from datetime import datetime
from buienradar.buienradar import (get_data, parse_data)
from buienradar.constants import (CONTENT, RAINCONTENT, SUCCESS)

# Function to convert datetime objects to strings
def convert_datetime(obj):
    if isinstance(obj, datetime):
        return obj.isoformat()  # Convert to ISO 8601 format
    raise TypeError(f'Object of type {obj.__class__.__name__} is not JSON serializable')

# Function to write data to CSV
def write_to_csv(data, filename='weather_data.csv'):
    with open(filename, mode='a', newline='', encoding='utf-8') as file:
        writer = csv.writer(file)
        # Write header if the file is empty
        if file.tell() == 0:
            writer.writerow(['RequestTimestamp', 'Measured', 'DateTime', 'ConditionCode', 'Condition', 'DetailedCondition', 'ExactCondition', 'Night', 
                             'Temperature', 'MinTemp', 'MaxTemp', 'SunChance', 'RainChance', 'Rain', 
                             'MinRain', 'MaxRain', 'Snow', 'WindForce', 'WindSpeed', 'WindDirection', 
                             'WindAzimuth', 'Image', 'StationName', 'Humidity', 'GroundTemperature', 
                             'Pressure', 'Visibility', 'RainLast24Hour', 'RainLastHour', 'FeelTemperature'])

        # Check if 'data' key exists
        if 'data' in data:
            # Extract general parameters
            station_name = data['data'].get('stationname', '')
            humidity = data['data'].get('humidity', '')
            ground_temperature = data['data'].get('groundtemperature', '')
            pressure = data['data'].get('pressure', '')
            visibility = data['data'].get('visibility', '')
            rain_last_24_hour = data['data'].get('rainlast24hour', '')
            rain_last_hour = data['data'].get('rainlasthour', '')
            feel_temperature = data['data'].get('feeltemperature', '')
            measured_time = data['data'].get('measured', '')  # Get the measured time from the API response

            # Get current date
            current_date = datetime.now().date()

            # Check if 'forecast' key exists
            if 'forecast' in data['data']:
                for forecast in data['data']['forecast']:
                    # Check if 'datetime' is a string or datetime object
                    forecast_datetime = forecast.get('datetime')
                    if isinstance(forecast_datetime, str):
                        forecast_datetime = datetime.fromisoformat(forecast_datetime)
                    elif not isinstance(forecast_datetime, datetime):
                        print(f"Invalid datetime format for forecast: {forecast}")
                        continue  # Skip this forecast if the datetime is not valid

                    # Only process today's forecast
                    if forecast_datetime.date() == current_date:
                        condition_code = forecast['condition']['condcode']
                        condition = forecast['condition']['condition']
                        detailed_condition = forecast['condition']['detailed']
                        exact_condition = forecast['condition']['exact']
                        night = forecast['condition']['night']
                        temperature = forecast['temperature']
                        mintemp = forecast['mintemp']
                        maxtemp = forecast['maxtemp']
                        sunchance = forecast['sunchance']
                        rainchance = forecast['rainchance']
                        rain = forecast['rain']
                        minrain = forecast['minrain']
                        maxrain = forecast['maxrain']
                        snow = forecast['snow']
                        windforce = forecast['windforce']
                        windspeed = forecast['windspeed']
                        winddirection = forecast['winddirection']
                        windazimuth = forecast['windazimuth']
                        image = forecast['condition']['image']

                        # Get the current measured time
                        request_timestamp = datetime.now().isoformat()

                        # Write the row to the CSV
                        writer.writerow([request_timestamp, measured_time, forecast_datetime.isoformat(), condition_code, condition, detailed_condition, exact_condition, night, 
                                         temperature, mintemp, maxtemp, sunchance, rainchance, rain, 
                                         minrain, maxrain, snow, windforce, windspeed, winddirection, 
                                         windazimuth, image, station_name, humidity, ground_temperature, 
                                         pressure, visibility, rain_last_24_hour, rain_last_hour, feel_temperature])
                        print(f"Data saved for {forecast_datetime}.")
                    else:
                        print(f"Skipping forecast for {forecast_datetime} as it is not today.")

            else:
                print("No forecast data found in the parsed result.")
        else:
            print("No data found in the parsed result.")

# Minutes to look ahead for precipitation forecast (5..120)
timeframe = 45

# GPS coordinates for the weather data (Eindhoven)
latitude = 51.4416
longitude = 5.4697

try:
    while True:
        # Get weather data
        result = get_data(latitude=latitude, longitude=longitude)

        if result.get(SUCCESS):
            data = result[CONTENT]
            raindata = result[RAINCONTENT]

            # Parse the data
            parsed_result = parse_data(data, raindata, latitude, longitude, timeframe)

            # Write the parsed result to CSV
            write_to_csv(parsed_result)

            # Print the result to the console (optional)
            print(json.dumps(parsed_result, default=convert_datetime, indent=4, ensure_ascii=False))

        else:
            print("Failed to retrieve data.")

        # Wait for 10 minutes before the next retrieval
        time.sleep(600)

except KeyboardInterrupt:
    print("Data retrieval stopped.")
