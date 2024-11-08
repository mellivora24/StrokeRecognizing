import os
import cv2
import time
import serial
import firebase_admin
from ultralytics import YOLO
from firebase_admin import credentials, firestore

os.environ['KMP_DUPLICATE_LIB_OK'] = 'True'

# Initialize YOLO model
try:
    model = YOLO("fall_detection.pt")
except Exception as e:
    print(f"Error loading YOLO model: {e}")
    exit(1)

# Initialize Firebase
try:
    cred = credentials.Certificate("stroke-app-hlt24e.json")
    firebase_admin.initialize_app(cred)
    db = firestore.client()
except Exception as e:
    print(f"Error initializing Firebase: {e}")
    exit(1)

# Initialize Serial for SIM communication
try:
    SIM = serial.Serial("/dev/ttyAMA0", 115200, timeout=1)
except serial.SerialException as e:
    print(f"Error initializing serial connection: {e}")
    exit(1)

# Load phone numbers
try:
    with open("phone_num.txt", "r") as file:
        phone_nums = [line.strip() for line in file if line.strip()]
except FileNotFoundError:
    print("Phone number file not found.")
    phone_nums = []

def make_call():
    """Make calls to each phone number in the list."""
    for phone_num in phone_nums:
        try:
            SIM.write(b"ATD" + phone_num.encode() + b";\r\n")
            time.sleep(10)
            SIM.write(b"ATH\r\n")
            time.sleep(5)
        except Exception as e:
            print(f"Error making call to {phone_num}: {e}")

def confirm_fall():
    """Check health parameters and make a call if any are abnormal."""
    users_ref = db.collection("con_nguoi")
    docs = users_ref.stream()
    for doc in docs:
        data = doc.to_dict()
        nhiet_do = data.get("nhiet_do")
        state = data.get("state")
        oxy = data.get("oxy")
        nhip_tim = data.get("nhip_tim")

        print(f"Temperature: {nhiet_do}°C, State: {state}, O2 Level: {oxy}%, Heart Rate: {nhip_tim} BPM")

        if state:  # Trigger call if 'state' is True
            print("State indicates fall detected; making call.")
            make_call()
        elif (nhiet_do and nhiet_do < 36) or (oxy and oxy < 92) or (nhip_tim and nhip_tim < 50):
            print("Warning: Health parameters are outside normal ranges!")
            make_call()
        else:
            print("Health parameters are within normal ranges.")

def on_fall_detected():
    """Action to take on fall detection."""
    print("Fall detected!")
    confirm_fall()

# Initialize video capture
cap = cv2.VideoCapture(0)
if not cap.isOpened():
    print("Error: Could not open video source.")
    exit(1)

# Track the last time health parameters were checked
last_check_time = time.time()

try:
    while True:
        success, frame = cap.read()
        if not success:
            print("Error: Could not read frame.")
            break

        # Run YOLO detection
        results = model.track(frame, persist=True, conf=0.5)
        if results and results[0].boxes.conf.numel() > 0:
            confidence = results[0].boxes.conf[0].item()
            if confidence > 0.5:
                on_fall_detected()

        # Display detection
        cv2.imshow("Fall Detection", results[0].plot())

        # Check health parameters every 5 seconds
        current_time = time.time()
        if current_time - last_check_time > 5:
            confirm_fall()
            last_check_time = current_time

        # Exit on pressing 'q'
        if cv2.waitKey(1) & 0xFF == ord("q"):
            break
finally:
    cap.release()
    SIM.close()
    cv2.destroyAllWindows()
