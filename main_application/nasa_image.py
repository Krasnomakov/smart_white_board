import requests
import sys
import os
import signal

# Global variable to handle termination
terminate = False

def handle_termination_signal(signum, frame):
    global terminate
    print("Termination signal received. Cleaning up and exiting.")
    terminate = True

# Register the SIGTERM signal handler
signal.signal(signal.SIGTERM, handle_termination_signal)

def get_nasa_image(api_key, date):
    global terminate
    try:
        url = "https://api.nasa.gov/planetary/apod"
        params = {
            "api_key": api_key,
            "date": date,
            "hd": True
        }

        response = requests.get(url, params=params, timeout=10)  # Add a timeout to avoid hanging
        if terminate:
            return

        if response.status_code == 200:
            data = response.json()
            image_url = data.get("url")
            title = data.get("title", "NASA Image")

            if image_url:
                img_response = requests.get(image_url, timeout=10)
                if terminate:
                    return

                if img_response.status_code == 200:
                    # Get the directory where the script is located
                    script_dir = os.path.dirname(os.path.abspath(__file__))

                    # Define the output paths
                    output_image_path = os.path.join(script_dir, "nasa_image_output.jpg")
                    title_file_path = os.path.join(script_dir, "nasa_image_title.txt")

                    # Save the image
                    with open(output_image_path, "wb") as handler:
                        handler.write(img_response.content)

                    # Save the title
                    with open(title_file_path, "w") as title_file:
                        title_file.write(title)

                    print(f"Image '{title}' downloaded successfully.")
                else:
                    print(f"Failed to download image from URL: {image_url}")
            else:
                print("No image URL found in the response.")
        else:
            print("Failed to retrieve image:", response.text)

    except requests.exceptions.RequestException as e:
        if terminate:
            print("Operation interrupted during a network request.")
        else:
            print("Network error:", str(e))

    except Exception as e:
        if terminate:
            print("Operation interrupted during execution.")
        else:
            print("An error occurred:", str(e))

if __name__ == "__main__":
    # Read the API key from an environment variable
    api_key = "3fNccc731lcukhnw2OavgKXSvdplpkYk02Cr9uWa"
    if not api_key:
        print("Error: NASA_API_KEY environment variable is not set.")
        sys.exit(1)

    if len(sys.argv) > 1:
        date = sys.argv[1]  # Date in YYYY-MM-DD format
    else:
        print("Usage: python nasa_image.py YYYY-MM-DD")
        sys.exit(1)

    # Check for termination flag periodically
    if not terminate:
        get_nasa_image(api_key, date)
