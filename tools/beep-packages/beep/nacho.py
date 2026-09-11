import email
import os
import smtplib
import sys

DEFAULT_NAME = 'Nacho'

def send(to, subject, text, name=DEFAULT_NAME, attachments=None):
    if isinstance(to, list):
        to = ', '.join([x.strip() for x in to])
    elif not isinstance(to, str):
        raise Exception('to is {} expected str or list'.format(type(to)))

    msg = email.MIMEMultipart.MIMEMultipart()
    msg['From'] = email.utils.formataddr([name, 'nacho@thisisbeep.com'])
    msg['To'] = to
    msg['Date'] = email.Utils.formatdate(localtime=True)
    msg['Subject'] = subject

    msg.attach(email.MIMEText.MIMEText(text))

    if attachments is None:
        attachments = []
    elif isinstance(attachments, str):
        attachments = [attachments,]

    for path in attachments:
        part = email.MIMEBase.MIMEBase('application', 'octet-stream')
        f = open(path, 'r')
        data = f.read()
        f.close()
        part.set_payload(data)
        email.Encoders.encode_base64(part)
        part.add_header('Content-Disposition',
                'attachment; filename="{}"'.format(os.path.basename(path)))
        msg.attach(part)

    smtp = smtplib.SMTP('localhost')
    smtp.sendmail(name, to, msg.as_string())
    smtp.close()
