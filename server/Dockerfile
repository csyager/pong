FROM gcc:latest AS build
WORKDIR /app
COPY src/ src/
RUN gcc -static -o server src/*.c -lrt -lpthread

FROM scratch
COPY --from=build /app/server /server
EXPOSE 9034/tcp 9034/udp
CMD ["/server"]
