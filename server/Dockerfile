# geist-online-relay -- GeistNative multiplayer session relay (UDP 47720)
FROM golang:1.24-alpine AS build
WORKDIR /src
COPY go.mod ./
COPY *.go ./
RUN CGO_ENABLED=0 go test ./... && CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o /relay .

FROM gcr.io/distroless/static:nonroot
COPY --from=build /relay /relay
USER nonroot
EXPOSE 47720/udp
ENTRYPOINT ["/relay"]
