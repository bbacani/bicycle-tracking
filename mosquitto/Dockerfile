# syntax=docker/dockerfile:1

FROM eclipse-mosquitto:2.0.18
WORKDIR /workspace/app

COPY ./config/mosquitto.conf /mosquitto/config/
COPY ./config/entrypoint.sh .

RUN chmod +x entrypoint.sh

EXPOSE 1883

CMD ["sh", "entrypoint.sh"]