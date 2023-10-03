import smtplib
from email.mime.text import MIMEText
from getpass import getpass

def send_email(sender, recipient, subject, message):
    # Set up the MIMEText object
    msg = MIMEText(message)
    msg['Subject'] = subject
    msg['From'] = sender
    msg['To'] = recipient

    # Connect to the SMTP server
    server = smtplib.SMTP('localhost', 2525)
    print("starting send")
    try:
        # Send the email
        server.sendmail(sender, [recipient], msg.as_string())
        print('Email sent successfully!')
    except smtplib.SMTPException as e:
        print('Failed to send email:', str(e))
    finally:
        # Disconnect from the server
        server.quit()

if __name__ == "__main__":
    sender_email = input('Enter sender email: ')
    recipient_email = input('Enter recipient email: ')
    subject = input('Enter email subject: ')
    message = input('Enter email message: ')

    send_email(sender_email, recipient_email, subject, message)
