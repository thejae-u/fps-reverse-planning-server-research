namespace AuthServer;

public class Result<T>
{
    public bool IsSuccess { get; }
    public T? Data { get; }
    public string ErrorCode { get; }
    public string ErrorMessage { get; }

    private Result(bool isSuccess, T? data, string errorCode, string errorMessage)
    {
        IsSuccess = isSuccess;
        Data = data;
        ErrorCode = errorCode;
        ErrorMessage = errorMessage;
    }

    public static Result<T> Success(T data)
    {
        if (data is null)
            throw new ArgumentNullException(nameof(data));

        return new Result<T>(true, data, "", "");
    }

    public static Result<T> Failure(string code, string message)
        => new Result<T>(false, default, code, message);
}
