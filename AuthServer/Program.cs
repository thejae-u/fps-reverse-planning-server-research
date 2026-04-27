using System.Text;
using AuthServer.Data;
using AuthServer.Hubs;
using AuthServer.OpenApi;
using AuthServer.Services;
using AuthServer.Services.Tcp;
using Microsoft.AspNetCore.Authentication.JwtBearer;
using Microsoft.EntityFrameworkCore;
using Microsoft.IdentityModel.Tokens;
using Microsoft.OpenApi;
using Scalar.AspNetCore;
using StackExchange.Redis;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddControllers();

// PostgreSQL configuration
var dbConnectionString = builder.Configuration.GetConnectionString("DefaultConnection");
builder.Services.AddDbContext<ApplicationDbContext>(options => options.UseNpgsql(dbConnectionString));

// Redis Configuration
var redisConnectionString = builder.Configuration["Redis:ConnectionString"] ?? builder.Configuration["Redis__ConnectionString"] ?? "redis:6379";
builder.Services.AddSingleton<IConnectionMultiplexer>(ConnectionMultiplexer.Connect(redisConnectionString));

// Cors for dev
builder.Services.AddCors(options =>
{
    options.AddPolicy("DevCors", policy =>
    {
        policy
            .WithOrigins("http://localhost:5500")
            .AllowAnyHeader()
            .AllowAnyMethod()
            .AllowCredentials();
    });
});

builder.Services.AddOpenApi("v1", options =>
{
    options.AddDocumentTransformer<BearerSecuritySchemeTransformer>();
});

// web socket
builder.Services.AddSignalR();

// global fields
builder.Services.Configure<MatchOptions>(builder.Configuration.GetSection("MatchOptions"));
builder.Services.Configure<TcpOptions>(builder.Configuration.GetSection("LogicServer"));

// Service DI
builder.Services.AddSingleton<LogicServerConnectionPool>();
builder.Services.AddSingleton<UserService>();
builder.Services.AddSingleton<JwtTokenService>();
builder.Services.AddSingleton<MatchService>();
builder.Services.AddHostedService<MatchWorker>();
builder.Services.AddLogging();

// JWT Configuation
var jwtIssuer = builder.Configuration["Jwt:Issuer"];
var jwtAudience = builder.Configuration["Jwt:Audience"];
var jwtSecretKey = builder.Configuration["Jwt:SecretKey"];

if (string.IsNullOrEmpty(jwtIssuer) || string.IsNullOrEmpty(jwtAudience) || string.IsNullOrEmpty(jwtSecretKey))
{
    throw new InvalidOperationException("JWT appsettings not set");
}

builder.Services.AddAuthentication(JwtBearerDefaults.AuthenticationScheme).AddJwtBearer(options =>
{
    options.TokenValidationParameters = new TokenValidationParameters
    {
        ValidateIssuer = true,
        ValidIssuer = jwtIssuer,
        ValidateAudience = true,
        ValidAudience = jwtAudience,
        ValidateIssuerSigningKey = true,
        IssuerSigningKey = new SymmetricSecurityKey(Encoding.UTF8.GetBytes(jwtSecretKey)),
        ValidateLifetime = true,
        ClockSkew = TimeSpan.Zero
    };

    // SignalR Hubs Authorize
    options.Events = new JwtBearerEvents
    {
        OnMessageReceived = context =>
        {
            var accessToken = context.Request.Query["access_token"];
            var path = context.HttpContext.Request.Path;

            if (!string.IsNullOrEmpty(accessToken) && path.StartsWithSegments("/hubs/match"))
            {
                context.Token = accessToken;
            }

            return Task.CompletedTask;
        }
    };
});

builder.Services.AddAuthorization();

var app = builder.Build();

if (app.Environment.IsDevelopment())
{
    app.MapOpenApi();
    app.MapScalarApiReference("/scalar", options =>
    {
        options.WithTitle("AuthServer API Reference");
    });
}

app.UseCors("DevCors");

app.UseAuthentication();
app.UseAuthorization();

app.MapControllers();

// Server Information Route
app.MapGet("/ping", () => "AuthServer v1.0 - OK");
app.MapGet("/health", () => new { status = "healthy", timestamp = DateTime.UtcNow });
app.MapGet("/version", () => "AuthServer v0.3.1-develop");
app.MapGet("/version/detail", () => new
{
    Version = "version 0.3.1",
    Status = "feature",
    FeatureBranch = new
    {
        Name = "feat/7-imp-web-server",
        Link = "https://thejaeu.com/fps-reverse-planning-server-research/tree/feat/7-impl-web-server"
    }
});
app.MapGet("/info", () => new
{
    info = "API Server for Native C++ Game Logic Server",
    detail = "Created thejaeu with AI"
});

// SignalR Match Hub Route
app.MapHub<MatchHub>("/hubs/match");

app.Run();
